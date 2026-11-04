/*
    KspHostCheck: checks that a VST3 instrument of the scanned list can be hosted, with no audio device and no window.
        KspHostCheck "Mini V4" [--block 512] [--rate 48000]
    Loads the instrument (first match by name in the Studio's plugin list), plays a note, renders audio and reports the peak; saves
    its state as text, loads the plugin again, restores the state and renders the same note. Exit 0 when the first instance sounds
    and the state round-trips (the restored one sounds too). Plays nothing out loud.
*/

#include <iostream>
#include <juce_events/juce_events.h>
#include "Host/PluginList.h"
#include "Host/TrackEffect.h"
#include "Host/Recorder.h"
#include "Host/TrackInstrument.h"

namespace
{

/** Peak of the audio a fresh note produces: note on, `blocks` blocks, note off. */
float playNote (ksp::host::TrackInstrument& inst, int block, int blocks, int note)
{
    std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
        if (b == blocks / 2)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        inst.process (midi, l.data(), r.data(), block);
        for (int i = 0; i < block; ++i)
            peak = juce::jmax (peak, std::abs (l[static_cast<std::size_t> (i)]), std::abs (r[static_cast<std::size_t> (i)]));
    }
    return peak;
}

} // namespace

/** --effect: loads an EFFECT of the scanned list, passes a noise burst through it, restores its state in a second instance and checks both
    outputs are finite, not silent and not the untouched input. */
int checkEffect (const juce::String& name, int block, double rate)
{
    ksp::host::PluginList list;
    const juce::PluginDescription* found = nullptr;
    auto effects = list.effects();
    for (const auto& d : effects)
        if (d.name.equalsIgnoreCase (name))
            found = &d;
    for (const auto& d : effects)
        if (found == nullptr && d.name.containsIgnoreCase (name))
            found = &d;
    if (found == nullptr)
    {
        std::cout << "No effect named like \"" << name << "\" in the list (" << effects.size() << " effects)." << std::endl;
        return 2;
    }
    std::cout << "Effect: " << found->name << " (" << found->manufacturerName << ")" << std::endl;

    juce::String error;
    auto first = ksp::host::TrackEffect::create (*found, rate, block, error);
    if (first == nullptr)
    {
        std::cout << "ERROR: cannot load: " << error << std::endl;
        return 1;
    }
    auto run = [&] (ksp::host::TrackEffect& fx, float& peakOut, float& difference) {
        juce::Random rnd (7);
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block)), dry;
        peakOut = 0.0f;
        difference = 0.0f;
        bool finite = true;
        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < block; ++i)
                l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = b < 40 ? 0.3f * (rnd.nextFloat() * 2.0f - 1.0f) : 0.0f;
            dry = l;
            fx.processStereo (l.data(), r.data(), block);
            for (int i = 0; i < block; ++i)
            {
                finite = finite && std::isfinite (l[static_cast<std::size_t> (i)]);
                peakOut = std::max (peakOut, std::abs (l[static_cast<std::size_t> (i)]));
                difference = std::max (difference, std::abs (l[static_cast<std::size_t> (i)] - dry[static_cast<std::size_t> (i)]));
            }
        }
        return finite;
    };
    float peak1 = 0, diff1 = 0, peak2 = 0, diff2 = 0;
    const bool finite1 = run (*first, peak1, diff1);
    std::cout << "Burst through it: peak " << peak1 << ", differs from the input by up to " << diff1 << std::endl;
    const auto state = first->stateBase64();
    const auto xml = first->descriptionXml();
    juce::PluginDescription restored;
    auto parsed = juce::XmlDocument::parse (xml);
    if (parsed == nullptr || ! restored.loadFromXml (*parsed))
    {
        std::cout << "ERROR: the description did not round-trip" << std::endl;
        return 1;
    }
    first.reset();
    auto second = ksp::host::TrackEffect::create (restored, rate, block, error);
    if (second == nullptr || ! second->setStateBase64 (state))
    {
        std::cout << "ERROR: cannot load again or restore the state" << std::endl;
        return 1;
    }
    const bool finite2 = run (*second, peak2, diff2);
    std::cout << "After restoring its state: peak " << peak2 << ", differs by up to " << diff2 << std::endl;
    const bool ok = finite1 && finite2 && peak1 > 0.0001f && peak2 > 0.0001f && diff1 > 0.0001f;
    std::cout << (ok ? "OK: it processes audio and the state round-trips." : "FAIL: silent, not finite or untouched.") << std::endl;
    return ok ? 0 : 1;
}

/** --record: the recorder with a synthetic signal. Manual mode records everything between start and stop; sync mode records nothing until
    the KeyStep's Start, then the music and the tail after its Stop, and ends by itself. The WAV files are read back and measured. */
int checkRecorder (double rate)
{
    const int block = 512;
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KspRecorderCheck");
    folder.deleteRecursively();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    int problems = 0;
    auto check = [&] (bool ok, const juce::String& what) {
        std::cout << (ok ? "  ok  " : "  FAIL ") << what << std::endl;
        problems += ok ? 0 : 1;
    };
    auto lengthAndPeak = [&] (const juce::String& path, double& seconds, float& peak) {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (juce::File (path)));
        if (r == nullptr)
            return false;
        seconds = static_cast<double> (r->lengthInSamples) / r->sampleRate;
        juce::AudioBuffer<float> b (2, static_cast<int> (r->lengthInSamples));
        r->read (&b, 0, static_cast<int> (r->lengthInSamples), 0, true, true);
        peak = b.getMagnitude (0, b.getNumSamples());
        return r->numChannels == 2 && r->bitsPerSample == 24;
    };
    std::vector<float> sine (static_cast<std::size_t> (block));
    long phase = 0;
    auto feed = [&] (ksp::host::Recorder& rec, double seconds, bool everyTap) {
        const int blocks = static_cast<int> (seconds * rate / block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i, ++phase)
                sine[static_cast<std::size_t> (i)] = 0.5f * std::sin (2.0f * 3.14159265f * 440.0f * static_cast<float> (phase) / static_cast<float> (rate));
            rec.write (ksp::host::Recorder::Mix, sine.data(), sine.data(), block);
            if (everyTap)
                for (int t = ksp::host::Recorder::Track1; t < ksp::host::Recorder::kTaps; ++t)
                    rec.write (t, sine.data(), sine.data(), block);
            rec.advance (block);
            juce::Thread::sleep (1);   // not faster than the disk thread can drain, like real time
        }
    };
    juce::String error;

    std::cout << "Manual mode, mix and stems, 1 s:" << std::endl;
    {
        ksp::host::Recorder rec;
        ksp::host::Recorder::Options o;
        o.mix = true;
        o.stems = true;
        o.syncToKeyStep = false;
        check (rec.start (folder, "manual", rate, o, error), "start " + error);
        feed (rec, 1.0, true);
        const auto files = rec.stop();
        check (files.size() == 6, "six files: mix, T1-T4, fx");
        for (const auto& f : files)
        {
            double sec = 0;
            float peak = 0;
            check (lengthAndPeak (f, sec, peak) && std::abs (sec - 1.0) < 0.02 && peak > 0.45f && peak < 0.55f, juce::File (f).getFileName() + " 1 s, peak 0.5");
        }
    }

    std::cout << "Sync mode: nothing before Start, the music, then the tail after Stop and it ends by itself:" << std::endl;
    {
        ksp::host::Recorder rec;
        ksp::host::Recorder::Options o;
        o.mix = true;
        o.stems = false;
        o.syncToKeyStep = true;
        o.tailSeconds = 0.5;
        check (rec.start (folder, "sync", rate, o, error), "start " + error);
        feed (rec, 0.5, false);   // armed, the KeyStep has not started
        check (rec.armed() && ! rec.recording() && rec.secondsRecorded() == 0.0, "armed, waiting for the KeyStep");
        rec.transport (0xFA);
        feed (rec, 1.0, false);
        check (rec.recording(), "recording after Start");
        rec.transport (0xFC);
        check (! rec.finished(), "the tail is still going");
        feed (rec, 1.0, false);   // more audio than the tail needs
        check (rec.finished() && ! rec.recording(), "finished by itself after the tail");
        const auto files = rec.stop();
        double sec = 0;
        float peak = 0;
        check (files.size() == 1 && lengthAndPeak (files[0], sec, peak) && std::abs (sec - 1.5) < 0.03, "the file is 1.5 s: the music plus the 0.5 s tail");
    }

    std::cout << "Armed and never started: no files are left:" << std::endl;
    {
        ksp::host::Recorder rec;
        ksp::host::Recorder::Options o;
        o.syncToKeyStep = true;
        check (rec.start (folder, "empty", rate, o, error), "start " + error);
        feed (rec, 0.2, false);
        check (rec.stop().isEmpty() && ! folder.getChildFile ("empty-mix.wav").existsAsFile(), "nothing was recorded and no file remains");
    }

    std::cout << (problems == 0 ? "OK: the recorder works." : "FAIL: see above.") << std::endl;
    folder.deleteRecursively();
    return problems == 0 ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));
    auto option = [&] (const char* name, const juce::String& fallback) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };
    if (args.isEmpty() || (args[0].startsWith ("--") && ! args.contains ("--record")))
    {
        std::cout << "Usage: KspHostCheck \"plugin name\" [--block 512] [--rate 48000]" << std::endl;
        return 2;
    }
    const int block = option ("--block", "512").getIntValue();
    const double rate = option ("--rate", "48000").getDoubleValue();
    if (args.contains ("--record"))
        return checkRecorder (rate);
    if (args.contains ("--effect"))
        return checkEffect (args[0], block, rate);

    ksp::host::PluginList list;
    const juce::PluginDescription* found = nullptr;
    auto instruments = list.instruments();
    for (const auto& d : instruments)
        if (d.name.equalsIgnoreCase (args[0]))
            found = &d;
    for (const auto& d : instruments)
        if (found == nullptr && d.name.containsIgnoreCase (args[0]))
            found = &d;
    if (found == nullptr)
    {
        std::cout << "No instrument named like \"" << args[0] << "\" in the list (" << instruments.size() << " instruments; scan first)." << std::endl;
        return 2;
    }
    std::cout << "Plugin: " << found->name << " (" << found->manufacturerName << ")" << std::endl;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    juce::String error;
    auto first = ksp::host::TrackInstrument::create (*found, rate, block, error);
    if (first == nullptr)
    {
        std::cout << "ERROR: cannot load: " << error << std::endl;
        return 1;
    }
    std::cout << "Loaded in " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) << " s, "
              << first->instance().getTotalNumOutputChannels() << " output channel(s)" << std::endl;

    const float peak1 = playNote (*first, block, 200, 60);
    std::cout << "Peak of a C3 note: " << peak1 << std::endl;

    const auto state = first->stateBase64();
    const auto xml = first->descriptionXml();
    std::cout << "State: " << state.length() << " characters of base64; description: " << xml.length() << " characters" << std::endl;

    // a second instance, from the saved text only: description XML + state, as the Studio project stores them
    juce::PluginDescription restored;
    if (auto parsed = juce::XmlDocument::parse (xml); parsed == nullptr || ! restored.loadFromXml (*parsed))
    {
        std::cout << "ERROR: the description did not round-trip" << std::endl;
        return 1;
    }
    first.reset();
    auto second = ksp::host::TrackInstrument::create (restored, rate, block, error);
    if (second == nullptr)
    {
        std::cout << "ERROR: cannot load again: " << error << std::endl;
        return 1;
    }
    if (! second->setStateBase64 (state))
    {
        std::cout << "ERROR: the state text is not valid" << std::endl;
        return 1;
    }
    const float peak2 = playNote (*second, block, 200, 60);
    std::cout << "Peak after restoring the state: " << peak2 << std::endl;
    std::cout << "State after restoring: " << (second->stateBase64() == state ? "identical text" : "differs (many plugins add counters or timestamps)") << std::endl;

    const bool ok = peak1 > 0.0001f && peak2 > 0.0001f;
    std::cout << (ok ? "OK: it sounds and the state round-trips." : "FAIL: silence.") << std::endl;
    return ok ? 0 : 1;
}
