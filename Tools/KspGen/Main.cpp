/*
    KspGen: experiment, generates ambient patterns (Source/Gen/Ambient.h) into a test slot of the KeyStep Pro.
    Without --write it only reads the slot and shows what it would write (a dry run). WRITES to the device with --write.

        KspGen [--to 16] [--seed N] [--root 2] [--scale 3] [--tracks 1-4] [--variations 4] [--no-scenes] [--no-slow] [--chains] [--mood classic|ethereal|dark|meditative] [--style techno|house|house-chill|house-melodic|dnb [--musical]] [--write] [--device "KeyStep Pro"]

    Any slot 1-16. Steps: read the slot and save it to Backups (ksp-pre-restore-P<n>-...), put patterns 1..variations of
    the chosen tracks over what the slot has (everything else in the slot stays), save the generated project to
    Documents/KeyStepPro Studio/Generated/, write, commit and read back to compare (Device::writeSlot, like KspRestore).
    Track 1 is written as a sequencer track, or with --style as a drum pattern (track 1 goes to drum mode).
*/

#include <iostream>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Gen/Arrangement.h"
#include "Ksp/Device.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));
    auto option = [&] (const char* name, const juce::String& fallback) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };

    const int to = option ("--to", "16").getIntValue();
    const bool doWrite = args.contains ("--write");
    ksp::gen::Params params;
    params.seed = static_cast<unsigned> (option ("--seed", "1").getIntValue());
    params.root = option ("--root", "2").getIntValue();
    params.scale = option ("--scale", "3").getIntValue();
    params.variations = option ("--variations", "4").getIntValue();
    params.slow = ! args.contains ("--no-slow");
    const auto mood = option ("--mood", "classic").toStdString();
    if (mood != "classic")
    {
        const auto m = ksp::gen::moodParams (mood);
        if (m.progression.empty())
        {
            std::cout << "--mood: classic, ethereal, dark or meditative" << std::endl;
            return 2;
        }
        params.progression = m.progression;
        if (! args.contains ("--root"))
            params.root = m.root;
        if (! args.contains ("--scale"))
            params.scale = m.scale;
    }
    const auto* style = ksp::gen::findStyle (option ("--style", "").toStdString());
    if (args.contains ("--style"))
    {
        if (style == nullptr)
        {
            std::cout << "--style: techno, house, house-chill, house-melodic or dnb" << std::endl;
            return 2;
        }
        const int variations = params.variations;
        params = ksp::gen::styleParams (*style, params.seed);   // the style's key and chords (track 1 goes to drum mode)
        params.variations = variations;
        if (args.contains ("--root"))
            params.root = option ("--root", "2").getIntValue();   // an explicit key or scale wins over the style's
        if (args.contains ("--scale"))
            params.scale = option ("--scale", "3").getIntValue();
    }
    params.chains = args.contains ("--chains");
    const auto tr = option ("--tracks", "1-4");
    const int firstTrack = tr.upToFirstOccurrenceOf ("-", false, false).getIntValue();
    const int lastTrack = tr.containsChar ('-') ? tr.fromFirstOccurrenceOf ("-", false, false).getIntValue() : firstTrack;
    if (to < 1 || to > 16 || firstTrack < 1 || lastTrack > 4 || firstTrack > lastTrack)
    {
        std::cout << "Usage: KspGen [--to 1-16] [--seed N] [--root 0-11] [--scale 1-5] [--tracks 1-4] [--variations 1-16] [--write]"
                  << std::endl;
        return 2;
    }

    juce::String error;
    auto device = ksp::Device::open (option ("--device", "KeyStep Pro"), error);
    if (device == nullptr)
    {
        std::cout << "ERROR: " << error << std::endl;
        return 1;
    }
    std::cout << "KeyStep Pro firmware " << device->identity().versionString() << std::endl;

    auto show = [] (const char* what) {
        return [what, shown = -1] (double x) mutable {
            const int pct = static_cast<int> (x * 100.0);
            if (pct / 10 != shown / 10)
            {
                std::cout << "\r" << what << ": " << pct << " %   " << std::flush;
                shown = pct;
            }
            return true;
        };
    };

    juce::File preFile;
    const auto saved = device->backupSlot (to, show ("Backing up the slot first"), preFile);
    std::cout << std::endl;
    if (saved.failed())
    {
        std::cout << "ERROR: " << saved.getErrorMessage() << "; nothing was sent." << std::endl;
        return 1;
    }
    std::cout << "Slot " << to << " now: " << ksp::summary (device->slotBefore()) << std::endl;
    std::cout << "Saved: " << preFile.getFullPathName() << std::endl;

    ksp::ProjectData project = device->slotBefore();
    const bool musical = style != nullptr && args.contains ("--musical");
    if (auto e = musical ? ksp::gen::applyArrangement (project, *style, params)
                : style != nullptr ? ksp::gen::applyStyle (project, *style, params, firstTrack, lastTrack)
                                  : ksp::gen::apply (project, params, firstTrack, lastTrack);
        ! e.empty())
    {
        std::cout << "ERROR: " << e << std::endl;
        return 1;
    }
    if (! args.contains ("--no-scenes") && ! musical)
        if (auto e = ksp::gen::applyScenes (project, params, style == nullptr); ! e.empty())
        {
            std::cout << "ERROR: " << e << std::endl;
            return 1;
        }
    std::cout << "Generated (seed " << params.seed << ", root " << ksp::pitchClassName (params.root) << ", "
              << ksp::scaleName (params.scale) << "): " << ksp::summary (project) << std::endl;
    for (int t = firstTrack; t <= lastTrack; ++t)
    {
        if (t == 1 && style != nullptr)
        {
            const auto d = ksp::model::readDrumPattern (project, 1);
            std::cout << "  track 1 drum pattern 1 (" << d.length << " steps): " << d.hits.size() << " hits" << std::endl;
            continue;
        }
        const auto pt = ksp::model::readPattern (project, t, 1);
        std::cout << "  track " << t << " pattern 1 (" << pt.length << " steps): " << ksp::gen::preview (pt) << std::endl;
    }

    const auto outDir = ksp::backupsFolder().getParentDirectory().getChildFile ("Generated");
    const auto outFile = outDir.getChildFile ("gen-seed" + juce::String (params.seed) + "-P" + juce::String (to) + "-"
                                              + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S") + ".json");
    if (outDir.createDirectory())
        outFile.replaceWithText (juce::String::fromUTF8 (ksp::toJson (project, device->identity().versionString()).c_str()));
    std::cout << "Project saved: " << outFile.getFullPathName() << std::endl;

    if (! doWrite)
    {
        std::cout << "Dry run: nothing was written. Add --write to send it to slot " << to << "." << std::endl;
        return 0;
    }

    const auto plan = ksp::writePlan (project, &device->slotBefore());   // only what differs from the slot as just read, plus the states
    std::cout << "Sends " << plan.requests.size() << " bulk SETs to slot " << to << std::endl;
    const auto wrote = device->writeSlot (plan, to, [] (double x) {
        std::cout << "\rWriting: " << static_cast<int> (x * 100.0) << " %   " << std::flush;
    });
    std::cout << std::endl;
    if (wrote.failed())
    {
        std::cout << "ERROR: " << wrote.getErrorMessage() << std::endl;
        return 1;
    }

    const auto after = device->readSlot (to, show ("Reading back"));
    std::cout << std::endl;
    if (! after.project)
    {
        std::cout << "ERROR: " << after.error << std::endl;
        return 1;
    }
    const auto problems = ksp::compareReadBack (project, *after.project);
    std::cout << "Slot " << to << " after: " << ksp::summary (*after.project) << std::endl;
    if (problems.empty())
    {
        std::cout << "Verified: every value reads back as written. On the KeyStep select project " << to << "." << std::endl;
        return 0;
    }
    std::cout << problems.size() << " value block(s) differ:" << std::endl;
    for (std::size_t i = 0; i < problems.size() && i < 30; ++i)
        std::cout << "  " << problems[i] << std::endl;
    std::cout << "The slot as it was is in " << preFile.getFullPathName() << std::endl;
    return 1;
}
