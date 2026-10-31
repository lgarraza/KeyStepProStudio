/*
    KspRestore: phase 1b, puts one project from a backup file back on the KeyStep Pro. It WRITES to the device.

        KspRestore --file backup.json --project N [--to M] [--write] [--no-verify] [--device "KeyStep Pro"]

    --project: the project in the file; --to: the slot written (default: the same number). Steps:
      1. The file is read and checked against the recall plan; the device must say firmware 2.5.20.
      2. Backup of the slot as it is now, to Documents/KeyStepPro Studio/Backups/ksp-pre-restore-P<M>-<date>.json.
         Without --write it stops here and shows what it would send (a dry run).
      3. Open session, every value of the project (states last, Source/Ksp/Restore.h), commit (about 3 s).
      4. The slot is read again and compared value for value (unless --no-verify).
    Only frames for slot M are sent (ksp::isWriteFor). Other slots and the global settings are never touched.
    The device reloads the project after the commit: unsaved edits on the KeyStep in that slot are lost.
*/

#include <iostream>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
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

    const auto filePath = option ("--file", {});
    const int from = option ("--project", "0").getIntValue();
    const int to = option ("--to", juce::String (from)).getIntValue();
    const bool doWrite = args.contains ("--write");
    const bool verify = ! args.contains ("--no-verify");
    if (filePath.isEmpty() || ! ksp::isValidProject (from) || ! ksp::isValidProject (to))
    {
        std::cout << "Usage: KspRestore --file backup.json --project N [--to M] [--write] [--no-verify]" << std::endl;
        return 2;
    }

    // 1. The file
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (filePath);
    const auto loaded = ksp::fromJson (file.loadFileAsString().toStdString());
    if (! loaded.backup)
    {
        std::cout << "ERROR: " << file.getFullPathName() << ": " << loaded.error << std::endl;
        return 2;
    }
    const ksp::ProjectData* source = nullptr;
    for (const auto& p : loaded.backup->projects)
        if (p.project == from)
            source = &p;
    if (source == nullptr)
    {
        std::cout << "ERROR: project " << from << " is not in " << file.getFileName() << std::endl;
        return 2;
    }
    ksp::ProjectData written = *source;
    written.project = to;
    const auto plan = ksp::writePlan (written);
    std::cout << "File: project " << from << " (saved " << source->saved << "): " << ksp::summary (*source) << std::endl;
    std::cout << "Sends " << plan.requests.size() << " bulk SETs to slot " << to << ", states last" << std::endl;
    if (plan.drumFixed > 0)
        std::cout << "Empty drum slots sent as the KeyStep stores them (" << plan.drumFixed << " values)" << std::endl;
    for (const auto& n : plan.notes)
        std::cout << "Note: " << n << std::endl;

    // 2. The device, and a backup of the slot as it is now
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

    if (! doWrite)
    {
        std::cout << "Dry run: nothing was written. Add --write to restore." << std::endl;
        return 0;
    }

    // 3. Write and commit
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto wrote = device->writeSlot (plan, to, [] (double x) {
        std::cout << "\rWriting: " << static_cast<int> (x * 100.0) << " %   " << std::flush;
    });
    std::cout << std::endl;
    if (wrote.failed())
    {
        std::cout << "ERROR: " << wrote.getErrorMessage() << std::endl;
        return 1;
    }
    std::cout << "Written in " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) << " s" << std::endl;

    if (! verify)
        return 0;

    // 4. Read back and compare
    const auto after = device->readSlot (to, show ("Reading back"));
    std::cout << std::endl;
    if (! after.project)
    {
        std::cout << "ERROR: " << after.error << std::endl;
        return 1;
    }
    const auto problems = ksp::compareReadBack (written, *after.project);
    std::cout << "Slot " << to << " after: " << ksp::summary (*after.project) << std::endl;
    if (problems.empty())
    {
        std::cout << "Verified: every value reads back as written." << std::endl;
        return 0;
    }
    std::cout << problems.size() << " value block(s) differ:" << std::endl;
    for (std::size_t i = 0; i < problems.size() && i < 30; ++i)
        std::cout << "  " << problems[i] << std::endl;
    std::cout << "The slot as it was is in " << preFile.getFullPathName() << std::endl;
    return 1;
}
