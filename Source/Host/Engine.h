/*
    Engine.h
    The Studio's audio engine: four hosted instruments, one per track of the KeyStep Pro, fed by the MIDI of the KeyStep (a track's
    notes arrive on its MIDI channel) and mixed to the audio device. JUCE.

    Threads. The audio callback never allocates, locks or logs: MIDI reaches it through two single-producer FIFOs (one filled by the
    MIDI input thread, one by the message thread for test notes), and an instrument is swapped with an "enabled / busy" pair of
    flags (the message thread switches it off, waits until the audio thread has left it, and only then touches it).
    Loading, replacing and state calls belong to the message thread.
*/

#pragma once

#include <array>
#include <atomic>
#include <juce_audio_devices/juce_audio_devices.h>
#include "../Mix/Limiter.h"
#include "../Mix/Transport.h"
#include "Recorder.h"
#include "TrackEffect.h"
#include "TrackInstrument.h"

namespace ksp::host
{

/** A single-producer, single-consumer queue of short MIDI messages (channel voice messages, at most 3 bytes). */
class MidiFifo
{
public:
    struct Event
    {
        std::array<std::uint8_t, 3> data {};
        std::uint8_t size = 0;
    };

    void push (const juce::MidiMessage& m) noexcept
    {
        const int n = m.getRawDataSize();
        if (n < 1 || n > 3 || m.getRawData()[0] < 0x80 || m.getRawData()[0] >= 0xF0)
            return;   // clock, Start / Stop, system exclusive: not for the instruments
        int a, sa, b, sb;
        fifo.prepareToWrite (1, a, sa, b, sb);
        if (sa < 1)
            return;   // full: the newest message is dropped
        auto& e = events[static_cast<std::size_t> (a)];
        e.size = static_cast<std::uint8_t> (n);
        for (int i = 0; i < n; ++i)
            e.data[static_cast<std::size_t> (i)] = m.getRawData()[i];
        fifo.finishedWrite (1);
    }

    template <typename Fn>
    void drain (Fn&& fn) noexcept
    {
        int a, sa, b, sb;
        const int ready = fifo.getNumReady();
        fifo.prepareToRead (ready, a, sa, b, sb);
        for (int i = 0; i < sa; ++i)
            fn (events[static_cast<std::size_t> (a + i)]);
        for (int i = 0; i < sb; ++i)
            fn (events[static_cast<std::size_t> (b + i)]);
        fifo.finishedRead (sa + sb);
    }

private:
    static constexpr int kCapacity = 1024;
    juce::AbstractFifo fifo { kCapacity };
    std::array<Event, kCapacity> events {};
};

class Engine : private juce::AudioIODeviceCallback
{
public:
    static constexpr int kTracks = 4;
    static constexpr int kInserts = 4;     // VST3 effects in series on each track, after the instrument
    static constexpr int kMasterFx = 4;    // VST3 effects in series on the whole mix, before the limiter
    // effect positions: the inserts of track t are t * kInserts + k, then the master chain
    static constexpr int kMasterFxBase = kTracks * kInserts, kFxPositions = kMasterFxBase + kMasterFx;

    Engine()
    {
        for (int t = 0; t < kTracks; ++t)
        {
            slots[static_cast<std::size_t> (t)].channel.store (t + 1);
            slots[static_cast<std::size_t> (t)].midi.ensureSize (4096);
        }
    }

    ~Engine() override { stop(); }

    /** Opens the default audio device (stereo out) and starts the callback; an empty string = fine, otherwise the reason. */
    juce::String start()
    {
        if (running)
            return {};
        auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
        if (error.isNotEmpty())
            return error;
        deviceManager.addAudioCallback (this);
        running = true;
        return {};
    }

    void stop()
    {
        if (! running)
            return;
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
        running = false;
    }

    bool isRunning() const noexcept { return running; }
    double sampleRate() const noexcept { return rate; }
    int blockSize() const noexcept { return block; }

    juce::AudioDeviceManager deviceManager;

    // ---- message thread: the instruments

    /** Puts `inst` on track `t` (0..3) in place of the current one (or none). */
    void setInstrument (int t, std::unique_ptr<TrackInstrument> inst)
    {
        auto& s = slots[static_cast<std::size_t> (t)];
        pause (s);
        s.inst = std::move (inst);
        if (s.inst != nullptr)
            s.inst->prepare (rate, block);
        s.enabled.store (s.inst != nullptr);
    }

    TrackInstrument* instrument (int t) noexcept { return slots[static_cast<std::size_t> (t)].inst.get(); }

    /** Runs `fn` with the instrument of track `t` out of the audio path (restoring its state, for example). */
    template <typename Fn>
    void withInstrumentPaused (int t, Fn&& fn)
    {
        auto& s = slots[static_cast<std::size_t> (t)];
        pause (s);
        if (s.inst != nullptr)
            fn (*s.inst);
        s.enabled.store (s.inst != nullptr);
    }

    // ---- message thread: the effects (VST3) of the inserts and the master chain

    /** Puts `fx` in effect position `pos` (see kMasterFxBase) in place of the current one (or none). */
    void setEffect (int pos, std::unique_ptr<TrackEffect> fx)
    {
        auto& s = fxSlots[static_cast<std::size_t> (pos)];
        pauseFx (s);
        s.fx = std::move (fx);
        if (s.fx != nullptr)
            s.fx->prepare (rate, block);
        s.enabled.store (s.fx != nullptr);
    }

    TrackEffect* effect (int pos) noexcept { return fxSlots[static_cast<std::size_t> (pos)].fx.get(); }
    void setEffectBypass (int pos, bool bypass) noexcept { fxSlots[static_cast<std::size_t> (pos)].bypass.store (bypass); }

    /** Runs `fn` with the effect at `pos` out of the audio path (restoring its state, for example). */
    template <typename Fn>
    void withEffectPaused (int pos, Fn&& fn)
    {
        auto& s = fxSlots[static_cast<std::size_t> (pos)];
        pauseFx (s);
        if (s.fx != nullptr)
            fn (*s.fx);
        s.enabled.store (s.fx != nullptr);
    }

    void setVolume (int t, float v) noexcept { slots[static_cast<std::size_t> (t)].volume.store (v); }
    void setMute (int t, bool m) noexcept { slots[static_cast<std::size_t> (t)].mute.store (m); }
    /** Solo: while any track is soloed, only the soloed ones sound (several can be). */
    void setSolo (int t, bool v) noexcept { slots[static_cast<std::size_t> (t)].solo.store (v); }
    /** The MIDI channel (1..16) the KeyStep's track `t` sends on. */
    void setChannel (int t, int ch) noexcept { slots[static_cast<std::size_t> (t)].channel.store (juce::jlimit (1, 16, ch)); }
    int channel (int t) const noexcept { return slots[static_cast<std::size_t> (t)].channel.load(); }

    // ---- MIDI in

    /** From the MIDI input thread (the KeyStep). The clock (24 per quarter note), Start, Continue and Stop drive the transport: the tempo of the
        synced delays, the position, and the gate of a recording that follows the KeyStep. */
    void midiFromDevice (const juce::MidiMessage& m) noexcept
    {
        if (m.getRawDataSize() == 1)
        {
            const auto status = m.getRawData()[0];
            if (status >= 0xF8)
            {
                transportState.message (status, juce::Time::getMillisecondCounterHiRes() * 0.001);
                recorderState.transport (status);
            }
            return;
        }
        fromDevice.push (m);
    }

    /** The tempo of the KeyStep's clock in BPM (120 until a clock arrives). */
    double bpm() const noexcept { return transportState.bpm(); }
    /** Playing or stopped, the position in clock ticks, and the Start / Stop counters (the KeyStep is the master). */
    const mix::Transport& transport() const noexcept { return transportState; }
    /** The WAV recorder (start / stop on the message thread). */
    Recorder& recorder() noexcept { return recorderState; }
    /** From the message thread (test notes). */
    void midiFromUi (const juce::MidiMessage& m) noexcept { fromUi.push (m); }

    /** Largest sample magnitude of the output since the last call. */
    float takePeak() noexcept { return peak.exchange (0.0f); }
    /** The most the master limiter pulled the gain down since the last call, in dB (0 or negative). */
    float takeReductionDb() noexcept { return reduction.exchange (0.0f); }

private:
    struct Slot
    {
        std::unique_ptr<TrackInstrument> inst;
        std::atomic<bool> enabled { false }, busy { false }, mute { false }, solo { false };
        std::atomic<float> volume { 0.8f };
        std::atomic<int> channel { 1 };
        juce::MidiBuffer midi;   // audio thread only
    };

    struct FxSlot
    {
        std::unique_ptr<TrackEffect> fx;
        std::atomic<bool> enabled { false }, busy { false }, bypass { false };
    };

    static void pauseFx (FxSlot& s)
    {
        s.enabled.store (false);
        while (s.busy.load())
            juce::Thread::sleep (1);
        if (s.fx != nullptr)
            s.fx->flush();
    }

    /** Audio thread: the effect of a position, in place (nothing if it is empty or bypassed). */
    static void runFx (FxSlot& s, float* l, float* r, int n) noexcept
    {
        if (! s.enabled.load() || s.bypass.load())
            return;
        s.busy.store (true);
        if (s.enabled.load() && s.fx != nullptr)
            s.fx->processStereo (l, r, n);
        s.busy.store (false);
    }

    static void pause (Slot& s)
    {
        s.enabled.store (false);
        while (s.busy.load())
            juce::Thread::sleep (1);
        if (s.inst != nullptr)
            s.inst->allNotesOff();
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        rate = d->getCurrentSampleRate();
        block = d->getCurrentBufferSizeSamples();
        for (auto& ch : scratch)
            ch.assign (static_cast<std::size_t> (block), 0.0f);
        for (auto& t : trackBuf)
            for (auto& ch : t)
                ch.assign (static_cast<std::size_t> (block), 0.0f);
        limiter.prepare (rate);
        for (auto& f : fxSlots)
        {
            pauseFx (f);
            if (f.fx != nullptr)
                f.fx->prepare (rate, block);
            f.enabled.store (f.fx != nullptr);
        }
        for (auto& s : slots)
        {
            pause (s);
            if (s.inst != nullptr)
                s.inst->prepare (rate, block);
            s.enabled.store (s.inst != nullptr);
        }
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c)
            juce::FloatVectorOperations::clear (out[c], numSamples);
        if (numOut < 1)
            return;

        for (auto& s : slots)
            s.midi.clear();
        auto route = [this] (const MidiFifo::Event& e) {
            const int ch = (e.data[0] & 0x0F) + 1;
            for (auto& s : slots)
                if (s.channel.load() == ch)
                    s.midi.addEvent (juce::MidiMessage (e.data[0], e.data[1], e.data[2]), 0);
        };
        fromDevice.drain (route);
        fromUi.drain (route);

        const int chunk = static_cast<int> (scratch[0].size());
        float level = 0.0f;
        for (auto& t : trackBuf)
            for (auto& ch : t)
                std::fill (ch.begin(), ch.end(), 0.0f);
        bool anySolo = false;
        for (auto& sl : slots)
            anySolo = anySolo || sl.solo.load();
        for (std::size_t ti = 0; ti < slots.size(); ++ti)
        {
            auto& s = slots[ti];
            if (! s.enabled.load())
                continue;
            s.busy.store (true);
            if (s.enabled.load() && s.inst != nullptr && chunk > 0)
            {
                const float gain = (s.mute.load() || (anySolo && ! s.solo.load())) ? 0.0f : s.volume.load();
                static const juce::MidiBuffer none;
                for (int pos = 0; pos < numSamples; pos += chunk)
                {
                    const int n = juce::jmin (chunk, numSamples - pos);
                    s.inst->process (pos == 0 ? s.midi : none, scratch[0].data(), scratch[1].data(), n);
                    for (int k = 0; k < kInserts; ++k)   // the track's own effects, before its fader
                        runFx (fxSlots[static_cast<std::size_t> (ti) * kInserts + static_cast<std::size_t> (k)], scratch[0].data(), scratch[1].data(), n);
                    for (int i = 0; i < n; ++i)
                    {
                        const float l = scratch[0][static_cast<std::size_t> (i)] * gain, r = scratch[1][static_cast<std::size_t> (i)] * gain;
                        trackBuf[ti][0][static_cast<std::size_t> (pos + i)] = l;   // post-fader: the track's stem
                        trackBuf[ti][1][static_cast<std::size_t> (pos + i)] = r;
                        out[0][pos + i] += l;
                        if (numOut > 1)
                            out[1][pos + i] += r;
                    }
                }
            }
            s.busy.store (false);
        }

        for (int t = 0; t < kTracks; ++t)   // the stems of the tracks: what each sends to the mix
            recorderState.write (Recorder::Track1 + t, trackBuf[static_cast<std::size_t> (t)][0].data(), trackBuf[static_cast<std::size_t> (t)][1].data(), numSamples);

        // the master chain, then the master limiter (which also never lets the output pass its ceiling)
        if (numOut > 1)
        {
            for (int k = 0; k < kMasterFx; ++k)   // the master chain, before the limiter
                runFx (fxSlots[static_cast<std::size_t> (kMasterFxBase + k)], out[0], out[1], numSamples);
            limiter.processStereo (out[0], out[1], numSamples);
            if (const float r = limiter.takeReductionDb(); r < reduction.load())
                reduction.store (r);
        }
        for (int c = 0; c < numOut; ++c)
            for (int i = 0; i < numSamples; ++i)
            {
                out[c][i] = juce::jlimit (-1.0f, 1.0f, out[c][i]);
                level = juce::jmax (level, std::abs (out[c][i]));
            }
        if (level > peak.load())
            peak.store (level);
        if (numOut > 1)
            recorderState.write (Recorder::Mix, out[0], out[1], numSamples);   // the final mix, after the limiter
        recorderState.advance (numSamples);
    }

    std::array<Slot, kTracks> slots;
    std::array<FxSlot, kFxPositions> fxSlots;
    MidiFifo fromDevice, fromUi;
    std::array<std::array<std::vector<float>, 2>, kTracks> trackBuf;   // each track's post-fader output, for its stem
    mix::Limiter limiter;
    std::atomic<float> reduction { 0.0f };
    mix::Transport transportState;
    Recorder recorderState;
    std::array<std::vector<float>, 2> scratch;
    std::atomic<float> peak { 0.0f };
    double rate = 48000.0;
    int block = 512;
    bool running = false;
};

} // namespace ksp::host
