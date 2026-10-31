/*
    Recorder.h
    Records what the Studio plays to WAV files (24 bit, stereo, the audio device's rate): the final mix, and/or one "stem" per track
    (what the track sends to the mix: instrument, inserts and fader). JUCE.

    Threads. start() / stop() belong to the message thread (they open and flush files). The audio thread only calls write() and advance(),
    which copy into the buffers of juce::AudioFormatWriter::ThreadedWriter (a lock-free FIFO drained to disk by a background thread), so
    nothing in it allocates, locks or touches the disk; a writer is taken out of the audio path with the same "active / busy" handshake the
    engine uses for the plugins. The MIDI thread calls transport().

    Gate. With "sync" the files are opened and armed but nothing is written until the KeyStep says Start (or Continue); at Stop the recording
    goes on for the tail (the reverbs and delays ring out after the music stops) and then closes itself (finished()). Without sync it
    records from start() until stop().
*/

#pragma once

#include <array>
#include <atomic>
#include <juce_audio_formats/juce_audio_formats.h>

namespace ksp::host
{

class Recorder
{
public:
    enum Tap { Mix = 0, Track1, Track2, Track3, Track4, kTaps };

    struct Options
    {
        bool mix = true, stems = false;
        bool syncToKeyStep = true;
        double tailSeconds = 5.0;
    };

    Recorder() : thread ("KspRecorder") {}
    ~Recorder() { stop(); }

    /** Opens the files `<folder>/<baseName>-mix.wav`, `-T1` .. `-T4`, `-fx` and arms the recording. False and `error` when it cannot. */
    bool start (const juce::File& folder, const juce::String& baseName, double sampleRate, const Options& o, juce::String& error)
    {
        stop();
        if (! o.mix && ! o.stems)
        {
            error = "Choose the mix, the stems or both.";
            return false;
        }
        if (! folder.createDirectory())
        {
            error = "Cannot create " + folder.getFullPathName();
            return false;
        }
        if (! thread.isThreadRunning())
            thread.startThread (juce::Thread::Priority::background);

        juce::WavAudioFormat wav;
        static const char* suffix[kTaps] { "mix", "T1", "T2", "T3", "T4" };
        written.clear();
        for (int t = 0; t < kTaps; ++t)
        {
            if (! (t == Mix ? o.mix : o.stems))
                continue;
            const auto file = folder.getNonexistentChildFile (baseName + "-" + suffix[t], ".wav", false);
            std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
            if (stream == nullptr)
            {
                error = "Cannot write " + file.getFullPathName();
                discard();
                return false;
            }
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
            if (w == nullptr)
            {
                error = "Cannot create the WAV file " + file.getFullPathName();
                discard();
                return false;
            }
            stream.release();   // the writer owns it now
            writers[static_cast<std::size_t> (t)] = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), thread, 1 << 17);   // about 3 seconds of audio of margin for the disk
            written.add (file.getFullPathName());
        }
        rate = sampleRate;
        sync = o.syncToKeyStep;
        tailSamples = static_cast<long> (std::max (0.0, o.tailSeconds) * sampleRate);
        samples.store (0);
        tailLeft.store (0);
        stopping.store (false);
        finishedFlag.store (false);
        gate.store (! sync);
        active.store (true);
        return true;
    }

    /** Closes (and flushes) the files; returns their paths. Safe to call when nothing is recording. A recording that never got any audio
        (armed, and the KeyStep never played) leaves no files. */
    juce::StringArray stop()
    {
        active.store (false);
        gate.store (false);
        while (busy.load())
            juce::Thread::sleep (1);
        for (auto& w : writers)
            w.reset();   // the destructor writes what is still in the buffer
        auto out = written;
        if (samples.load() == 0)
        {
            for (const auto& f : out)
                juce::File (f).deleteFile();
            out.clear();
        }
        written.clear();
        return out;
    }

    bool armed() const noexcept { return active.load(); }
    bool recording() const noexcept { return active.load() && gate.load(); }
    /** The KeyStep stopped, the tail has been recorded and the gate closed: the caller should stop() and report. */
    bool finished() const noexcept { return finishedFlag.load(); }
    double secondsRecorded() const noexcept { return rate > 0.0 ? static_cast<double> (samples.load()) / rate : 0.0; }
    const juce::StringArray& files() const noexcept { return written; }

    /** MIDI thread: the KeyStep's Start / Continue (0xFA, 0xFB) open the gate, Stop (0xFC) starts the tail. */
    void transport (std::uint8_t status) noexcept
    {
        if (! active.load() || ! sync)
            return;
        if (status == 0xFA || status == 0xFB)
        {
            stopping.store (false);
            gate.store (true);
        }
        else if (status == 0xFC && gate.load())
        {
            tailLeft.store (tailSamples);
            stopping.store (true);
        }
    }

    /** Audio thread: `n` samples of one tap. */
    void write (int tap, const float* left, const float* right, int n) noexcept
    {
        if (! active.load())
            return;
        busy.store (true);
        if (active.load() && gate.load())
            if (auto& w = writers[static_cast<std::size_t> (tap)])
            {
                const float* data[2] { left, right };
                w->write (data, n);
            }
        busy.store (false);
    }

    /** Audio thread, once per block after every tap was written: counts the time and closes the gate when the tail is over. */
    void advance (int n) noexcept
    {
        if (! active.load() || ! gate.load())
            return;
        samples.fetch_add (n);
        if (stopping.load() && tailLeft.fetch_sub (n) - n <= 0)
        {
            gate.store (false);
            stopping.store (false);
            finishedFlag.store (true);
        }
    }

private:
    void discard()
    {
        for (auto& w : writers)
            w.reset();
        written.clear();
    }

    juce::TimeSliceThread thread;
    std::array<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>, kTaps> writers;
    juce::StringArray written;
    std::atomic<bool> active { false }, busy { false }, gate { false }, stopping { false }, finishedFlag { false };
    std::atomic<long> samples { 0 }, tailLeft { 0 };
    long tailSamples = 0;
    double rate = 48000.0;
    bool sync = true;
};

} // namespace ksp::host
