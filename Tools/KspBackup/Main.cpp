/*
    KspBackup: phase 1a, backs up the KeyStep Pro to a JSON file. READ-ONLY: it only sends identity, GET, bulk GET and
    "open read session" frames (ksp::isReadOnlyRequest); it never writes settings or projects.

        KspBackup [--projects 1-16 | 1,3,5] [--no-globals] [--out file.json] [--device "KeyStep Pro"]

    Default: the 129 global settings and all 16 projects, as saved in flash (unsaved edits on the device are not in
    it), into Documents/KeyStepPro Studio/Backups/ksp-backup-<date>.json. The file is rewritten after each project, so
    an interrupted backup keeps the projects already read. Format: the KSCON page's (see Source/Ksp/Backup.h).
*/

#include <iostream>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Ksp/Backup.h"
#include "Ksp/MidiLink.h"

namespace
{

std::vector<int> parseProjects (const juce::String& spec)
{
    std::vector<int> out;
    for (const auto& part : juce::StringArray::fromTokens (spec, ",", ""))
    {
        const auto a = part.upToFirstOccurrenceOf ("-", false, false).trim().getIntValue();
        const auto b = part.containsChar ('-') ? part.fromFirstOccurrenceOf ("-", false, false).trim().getIntValue() : a;
        for (int p = a; p <= b; ++p)
            if (ksp::isValidProject (p) && std::find (out.begin(), out.end(), p) == out.end())
                out.push_back (p);
    }
    return out;
}

std::string isoNow()
{
    return juce::Time::getCurrentTime().toISO8601 (true).toStdString();
}

} // namespace

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

    const auto projects = parseProjects (option ("--projects", "1-16"));
    const bool withGlobals = ! args.contains ("--no-globals");
    if (projects.empty())
    {
        std::cout << "--projects: nothing in 1..16" << std::endl;
        return 2;
    }

    auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (option ("--out", {}));
    if (option ("--out", {}).isEmpty())
        outFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile ("KeyStepPro Studio").getChildFile ("Backups")
                      .getChildFile ("ksp-backup-" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S") + ".json");
    if (! outFile.getParentDirectory().createDirectory())
    {
        std::cout << "Cannot create " << outFile.getParentDirectory().getFullPathName() << std::endl;
        return 2;
    }

    juce::String error;
    auto link = ksp::MidiLink::open (option ("--device", "KeyStep Pro"), error);
    if (link == nullptr)
    {
        std::cout << "ERROR: " << error << std::endl;
        return 1;
    }

    auto ask = [&] (const ksp::Bytes& f, ksp::MidiLink::Match match) -> std::optional<ksp::Bytes> {
        if (! ksp::isReadOnlyRequest (f))
        {
            jassertfalse;   // never writes
            return std::nullopt;
        }
        return match ? link->request (f, match) : link->send (f, nullptr);
    };

    // The plan was checked against firmware 2.5.20 only.
    const auto idReply = ask (ksp::identityRequest(), [] (const ksp::Bytes& f) { return ksp::parseIdentity (f).has_value(); });
    if (! idReply)
    {
        std::cout << "ERROR: the KeyStep Pro does not answer. Check the cable and that MIDI Control Center is closed." << std::endl;
        return 1;
    }
    const auto identity = *ksp::parseIdentity (*idReply);
    std::cout << "KeyStep Pro firmware " << identity.versionString() << std::endl;
    if (! identity.isKnownFirmware())
    {
        std::cout << "ERROR: only firmware 2.5.20 is supported." << std::endl;
        return 1;
    }

    ksp::BackupData backup;
    backup.saved = isoNow();
    backup.firmware = identity.versionString();

    auto save = [&] {
        if (! outFile.replaceWithText (juce::String::fromUTF8 (ksp::toJson (backup).c_str())))
        {
            std::cout << "ERROR: cannot write " << outFile.getFullPathName() << std::endl;
            return false;
        }
        return true;
    };

    const auto started = juce::Time::getMillisecondCounterHiRes();

    if (withGlobals)
    {
        int missing = 0;
        for (const auto& g : ksp::kGlobalSettings)
        {
            const int id = g.id;
            const auto r = ask (ksp::globalGet (id), [id] (const ksp::Bytes& f) { return ksp::parseGlobal (f, id).has_value(); });
            backup.globals.push_back ({ id, r ? *ksp::parseGlobal (*r, id) : ksp::kUnreadable });
            missing += r ? 0 : 1;
        }
        std::cout << "Globals: " << backup.globals.size() - static_cast<std::size_t> (missing) << " of "
                  << backup.globals.size() << " read" << std::endl;
        if (! save())
            return 1;
    }

    const ksp::BulkRead bulkRead = [&] (int project, std::uint8_t prm, std::uint8_t it,
                                        const std::vector<std::uint8_t>& path, std::uint8_t count) {
        const auto r = ask (ksp::bulkGet (project, prm, it, path, count), [=] (const ksp::Bytes& f) {
            return ksp::parseBulkValues (f, project, prm, it, path, count).has_value();
        });
        return r ? ksp::parseBulkValues (*r, project, prm, it, path, count) : std::nullopt;
    };

    int done = 0;
    for (const int p : projects)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        ask (ksp::openSession (p), nullptr);
        int lastShown = -1;
        const auto r = ksp::readProject (p, bulkRead, [&] (double x) {
            const int pct = static_cast<int> (x * 100.0);
            if (pct / 10 != lastShown / 10)
            {
                std::cout << "\rProject " << p << " (" << (done + 1) << "/" << projects.size() << "): " << pct << " %   " << std::flush;
                lastShown = pct;
            }
            return true;
        }, isoNow());

        if (! r.project)
        {
            std::cout << std::endl << "ERROR: " << r.error << std::endl;
            std::cout << "Saved what was read so far: " << outFile.getFullPathName() << std::endl;
            return 1;
        }
        backup.projects.push_back (*r.project);
        ++done;
        std::cout << "\rProject " << p << ": " << ksp::summary (*r.project) << "  ("
                  << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) << " s)" << std::endl;
        if (! save())
            return 1;
    }

    std::cout << "Done in " << juce::String ((juce::Time::getMillisecondCounterHiRes() - started) / 60000.0, 1)
              << " min: " << outFile.getFullPathName() << std::endl;
    return 0;
}
