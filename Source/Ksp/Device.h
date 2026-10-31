/*
    Device.h
    A connected KeyStep Pro (JUCE): identity check, reading a project slot, and writing one with a backup first.
    Shared by Tools/KspRestore and the KeyStepPro Studio app. Not for the audio thread; calls block (a worker thread
    or a console tool), progress callbacks run on the calling thread.

    Writes only go through writeSlot(): firmware 2.5.20 only (any other firmware opens read-only: canWrite()), frames for that one slot only (ksp::isWriteFor),
    and only after backupSlot() has saved the slot as it is now.
*/

#pragma once

#include <juce_core/juce_core.h>
#include "Backup.h"
#include "MidiLink.h"
#include "Restore.h"

namespace ksp
{

inline std::string isoNow()
{
    return juce::Time::getCurrentTime().toISO8601 (true).toStdString();
}

inline juce::File backupsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("KeyStepPro Studio").getChildFile ("Backups");
}

class Device
{
public:
    /** Opens the port and checks the identity; nullptr and `error` when it cannot be used (also for other firmware). */
    static std::unique_ptr<Device> open (const juce::String& portMatch, juce::String& error)
    {
        auto link = MidiLink::open (portMatch, error);
        if (link == nullptr)
            return nullptr;
        std::unique_ptr<Device> d (new Device (std::move (link)));
        const auto reply = d->link->request (identityRequest(), [] (const Bytes& f) { return parseIdentity (f).has_value(); });
        if (! reply)
        {
            error = "The KeyStep Pro does not answer. Check the cable and that MIDI Control Center is closed.";
            return nullptr;
        }
        d->id = *parseIdentity (*reply);
        return d;   // a firmware other than 2.5.20 is not refused: it can be read, never written (canWrite)
    }

    const Identity& identity() const noexcept { return id; }

    /** Writes are only allowed on the firmware the protocol was verified on; the app stays behind Arturia's releases until a new one is tested. */
    bool canWrite() const noexcept { return id.isKnownFirmware(); }

    /** Notes, clock and Start/Stop from the KeyStep (MidiLink::setListener); called on the MIDI thread. */
    void setListener (std::function<void (const juce::MidiMessage&)> fn) { link->setListener (std::move (fn)); }

    /** Reads project `project` as saved in flash (about 40 s). */
    ReadResult readSlot (int project, const Progress& progress)
    {
        link->send (openSession (project), nullptr);
        return readProject (project, bulkReader(), progress, isoNow());
    }

    /** Saves the slot as it is now into the Backups folder; the file, or an error. */
    juce::Result backupSlot (int project, const Progress& progress, juce::File& saved)
    {
        auto r = readSlot (project, progress);
        if (! r.project)
            return juce::Result::fail (r.error);
        BackupData b;
        b.saved = isoNow();
        b.firmware = id.versionString();
        b.projects.push_back (*r.project);
        lastRead = *r.project;
        saved = backupsFolder().getChildFile ("ksp-pre-restore-P" + juce::String (project) + "-"
                                              + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S") + ".json");
        if (! saved.getParentDirectory().createDirectory() || ! saved.replaceWithText (juce::String::fromUTF8 (toJson (b).c_str())))
            return juce::Result::fail ("Cannot write " + saved.getFullPathName());
        return juce::Result::ok();
    }

    /** Reads again, from the saved copy of slot `project`, the blocks of one pattern of a track (about a second): the updates to put into the
        project shown. What was edited on the KeyStep and not saved there (Project > Save) is not in the saved copy. */
    std::optional<std::vector<BlockUpdate>> readPatternBlocks (int project, int track, int pattern, std::string& error)
    {
        link->send (openSession (project), nullptr);
        return readBlocks (project, patternRequests (item::track (track), pattern), bulkReader(), nullptr, error);
    }

    /** The slot as read by the last backupSlot() (for the summary shown before overwriting). */
    const ProjectData& slotBefore() const noexcept { return lastRead; }

    /** Sends the plan to `slot` and commits (about 3 s). Needs backupSlot(slot) to have succeeded just before. */
    juce::Result writeSlot (const WritePlan& plan, int slot, const std::function<void (double)>& progress)
    {
        if (! canWrite())
            return juce::Result::fail ("Firmware " + juce::String (id.versionString()) + " is not tested: only 2.5.20 can be written.");
        if (! isValidProject (slot) || lastRead.project != slot)
            return juce::Result::fail ("Back up the slot before writing it.");
        auto send = [&] (const Bytes& f) {
            if (! isWriteFor (f, slot))
            {
                jassertfalse;
                return false;
            }
            link->send (f, nullptr);
            return true;
        };
        link->setGapMs (12);
        bool ok = send (openSession (slot));
        for (std::size_t n = 0; ok && n < plan.requests.size(); ++n)
        {
            const auto& q = plan.requests[n];
            ok = send (bulkSet (slot, q.param, q.item, q.path, q.values));
            if (progress && n % 256 == 0)
                progress (static_cast<double> (n) / static_cast<double> (plan.requests.size()));
        }
        ok = ok && send (commit (slot));
        juce::Thread::sleep (3000);
        link->setGapMs (20);
        lastRead = {};   // the slot changed: a new write needs a new backup
        return ok ? juce::Result::ok() : juce::Result::fail ("A frame was refused by the write guard.");
    }

private:
    explicit Device (std::unique_ptr<MidiLink> l) : link (std::move (l)) {}

    /** Bulk GET through the link, retried, for readProject / readBlocks. */
    BulkRead bulkReader()
    {
        return [this] (int p, std::uint8_t prm, std::uint8_t it, const std::vector<std::uint8_t>& path, std::uint8_t count) -> std::optional<Bytes> {
            const auto r = link->request (bulkGet (p, prm, it, path, count), [=] (const Bytes& f) {
                return parseBulkValues (f, p, prm, it, path, count).has_value();
            });
            return r ? parseBulkValues (*r, p, prm, it, path, count) : std::nullopt;
        };
    }

    std::unique_ptr<MidiLink> link;
    Identity id;
    ProjectData lastRead;
};

} // namespace ksp
