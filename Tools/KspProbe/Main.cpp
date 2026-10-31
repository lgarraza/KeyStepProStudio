/*
    KspProbe: phase 0 connection test with the KeyStep Pro. READ-ONLY: it only sends identity, GET, bulk GET and
    "open read session" frames (ksp::isReadOnlyRequest); it never writes settings or projects.

        KspProbe [--list] [--scan] [--device "KeyStep Pro"] [--project 1..16] [--out report.txt]

    --scan also counts the valid (saved) patterns of every track in all 16 projects.

    Prints what it reads and saves the same text to the report file (default ksp_probe_report.txt next to where
    it runs), so it can be pasted back and compared with MIDI Control Center.
*/

#include <iostream>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Ksp/MidiLink.h"
#include "Ksp/Protocol.h"

namespace
{

struct Report
{
    juce::String text;
    void line (const juce::String& s = {})
    {
        std::cout << s << std::endl;
        text << s << "\n";
    }
};

struct Global
{
    int id;
    const char* name;
};

// A few globals from KSCON's map (findings.md, "Global settings map"), to compare with MCC / Utility.
const Global kGlobals[] {
    { 2, "Launch quantize (track 1)" }, { 61, "Velocity curve" }, { 62, "Transport send" },
    { 63, "Transport receive" },        { 64, "Clock send" },     { 65, "Clock receive" },
    { 70, "Track/drum MIDI ch (id 70)" }, { 71, "Track/drum MIDI ch (id 71)" }, { 72, "Track/drum MIDI ch (id 72)" },
    { 73, "Track/drum MIDI ch (id 73)" }, { 74, "Track/drum MIDI ch (id 74)" }, { 80, "Global MIDI channel" },
    { 115, "Time signature" },          { 136, "MIDI Out 2 mode" }, { 152, "Brightness" },
};

} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // MIDI devices need the message manager on some platforms

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    auto option = [&] (const char* name, const juce::String& fallback) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };

    const auto deviceMatch = option ("--device", "KeyStep Pro");
    const int project = option ("--project", "1").getIntValue();
    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (option ("--out", "ksp_probe_report.txt"));

    Report rep;
    rep.line ("KspProbe (read-only) " + juce::Time::getCurrentTime().toString (true, true));
    rep.line ("MIDI inputs:");
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        rep.line ("  " + d.name);
    rep.line ("MIDI outputs:");
    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        rep.line ("  " + d.name);

    if (args.contains ("--list"))
        return 0;

    if (! ksp::isValidProject (project))
    {
        rep.line ("--project must be 1..16");
        return 2;
    }

    juce::String error;
    auto link = ksp::MidiLink::open (deviceMatch, error);
    if (link == nullptr)
    {
        rep.line ("ERROR: " + error);
        outFile.replaceWithText (rep.text);
        return 1;
    }
    rep.line ("Using in \"" + link->inputName() + "\", out \"" + link->outputName() + "\"");

    int requests = 0, answered = 0;
    const auto started = juce::Time::getMillisecondCounterHiRes();

    auto ask = [&] (const ksp::Bytes& f, ksp::MidiLink::Match match) -> std::optional<ksp::Bytes> {
        if (! ksp::isReadOnlyRequest (f))
        {
            jassertfalse;   // the probe never writes
            return std::nullopt;
        }
        ++requests;
        auto r = match ? link->request (f, match) : link->send (f, nullptr);
        if (r)
            ++answered;
        return r;
    };

    // 1. Identity
    rep.line();
    if (auto r = ask (ksp::identityRequest(), [] (const ksp::Bytes& f) { return ksp::parseIdentity (f).has_value(); }))
    {
        const auto id = *ksp::parseIdentity (*r);
        rep.line ("Identity: " + juce::String (ksp::hex (*r)));
        rep.line ("Firmware " + juce::String (id.versionString()) + (id.isKnownFirmware() ? " (known: 2.5.20)"
                                                                                              : " (NOT 2.5.20: writing would stay blocked)"));
    }
    else
    {
        rep.line ("Identity: no reply. Check the cable and that MIDI Control Center is closed.");
    }

    // 2. Globals
    rep.line();
    rep.line ("Globals (compare with MCC > Device Settings or Utility on the KeyStep):");
    for (const auto& g : kGlobals)
    {
        const int id = g.id;
        auto r = ask (ksp::globalGet (id), [id] (const ksp::Bytes& f) { return ksp::parseGlobal (f, id).has_value(); });
        rep.line (juce::String (id).paddedLeft (' ', 5) + "  " + juce::String (g.name).paddedRight (' ', 30)
                  + (r ? juce::String (*ksp::parseGlobal (*r, id)) : juce::String ("no reply")));
    }

    // 2b. --scan: which of the 16 projects hold saved patterns (pattern state 3 = valid, 2 = default)
    if (args.contains ("--scan"))
    {
        rep.line();
        rep.line ("Saved patterns per project (valid patterns of track 1 / 2 / 3 / 4):");
        for (int p = 1; p <= 16; ++p)
        {
            ask (ksp::openSession (p), nullptr);
            juce::String row = "  project " + juce::String (p).paddedLeft (' ', 2) + ":";
            for (int t = 1; t <= 4; ++t)
            {
                const auto it = ksp::item::track (t);
                int valid = 0, unread = 0;
                for (int pat = 1; pat <= 16; ++pat)
                {
                    const std::vector<std::uint8_t> idx { static_cast<std::uint8_t> (pat) };
                    const auto f = ksp::bulkGet (p, ksp::param::patternState, it, idx, 1);
                    const auto r = ask (f, [=] (const ksp::Bytes& reply) {
                        return ksp::parseBulk (reply, p, ksp::param::patternState, it, idx, 1).has_value();
                    });
                    if (! r)
                        ++unread;
                    else if ((*ksp::parseBulk (*r, p, ksp::param::patternState, it, idx, 1))[0] == 3)
                        ++valid;
                }
                row << "  " << juce::String (valid).paddedLeft (' ', 2) << (unread > 0 ? "(" + juce::String (unread) + "?)" : "");
            }
            rep.line (row);
        }
    }

    // 3. One project: what is SAVED in flash (a read session copies flash, not the live state)
    rep.line();
    rep.line ("Project " + juce::String (project) + " as saved in flash:");
    ask (ksp::openSession (project), nullptr);

    auto bulk = [&] (std::uint8_t paramId, std::uint8_t itemId, std::vector<std::uint8_t> idx, std::uint8_t n) {
        const auto f = ksp::bulkGet (project, paramId, itemId, idx, n);
        auto r = ask (f, [=] (const ksp::Bytes& reply) {
            return ksp::parseBulk (reply, project, paramId, itemId, idx, n).has_value();
        });
        return r ? ksp::parseBulk (*r, project, paramId, itemId, idx, n) : std::nullopt;
    };

    for (int t = 1; t <= 4; ++t)
    {
        const auto it = ksp::item::track (t);
        const auto state = bulk (ksp::param::trackState, it, { 1 }, 1);
        rep.line();
        rep.line ("Track " + juce::String (t) + "  block state: "
                  + (state ? juce::String ((*state)[0]) + ((*state)[0] == 3 ? " (valid)" : " (default)") : juce::String ("no reply")));

        juce::String counts = "  steps:", roots = "  root: ", scales = "  scale:";
        for (int p = 1; p <= 16; ++p)
        {
            const auto pi = static_cast<std::uint8_t> (p);
            const auto c = bulk (ksp::param::seqStepCount, it, { pi }, 1);
            const auto r = bulk (ksp::param::seqRoot, it, { pi }, 1);
            const auto s = bulk (ksp::param::seqScale, it, { pi }, 1);
            counts << " " << (c ? juce::String ((*c)[0] + 1) : "?").paddedLeft (' ', 3);
            roots << " " << (r ? juce::String (ksp::pitchClassName ((*r)[0])) : "?").paddedLeft (' ', 3);
            scales << " " << (s ? juce::String ((*s)[0]) : "?").paddedLeft (' ', 3);
        }
        rep.line ("  pattern" + juce::String ("  1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16"));
        rep.line (counts);
        rep.line (roots);
        rep.line (scales);

        // Every pattern: state, active steps and notes. A pattern has 192 note slots addressed [pattern, voice 1-3,
        // slot 1-64]; param 0x32 gives the 0-based step a slot belongs to (0x7F = empty slot), as KSCON's editor reads them.
        juce::String states = "  state:";
        juce::StringArray details;
        for (int p = 1; p <= 16; ++p)
        {
            const auto pi = static_cast<std::uint8_t> (p);
            const auto st = bulk (ksp::param::patternState, it, { pi }, 1);
            states << " " << (st ? juce::String ((*st)[0]) : "?").paddedLeft (' ', 3);

            int activeSteps = 0;
            for (std::uint8_t first = 1; first <= 49; first += 16)
                if (const auto on = bulk (ksp::param::stepActive, it, { pi, 1, first }, 16))
                    for (auto v : *on)
                        activeSteps += v != 0 ? 1 : 0;

            juce::StringArray notes;
            int unread = 0;
            for (std::uint8_t voice = 1; voice <= 3; ++voice)
                for (std::uint8_t first = 1; first <= 49; first += 16)
                {
                    const auto steps = bulk (ksp::param::noteStep, it, { pi, voice, first }, 16);
                    if (! steps) { ++unread; continue; }
                    bool any = false;
                    for (auto v : *steps)
                        any = any || v <= 63;
                    if (! any)
                        continue;
                    const auto pitches = bulk (ksp::param::notePitch, it, { pi, voice, first }, 16);
                    for (std::size_t i = 0; i < 16; ++i)
                        if ((*steps)[i] <= 63)
                            notes.add (juce::String ((*steps)[i] + 1) + ":"
                                       + (pitches ? juce::String (ksp::noteName ((*pitches)[i])) : juce::String ("?")));
                }

            if (activeSteps > 0 || ! notes.isEmpty() || unread > 0)
                details.add ("  pattern " + juce::String (p) + ": " + juce::String (activeSteps) + " active steps, "
                             + juce::String (notes.size()) + " notes" + (unread > 0 ? ", " + juce::String (unread) + " blocks unread" : "")
                             + (notes.isEmpty() ? "" : " (step:note) " + notes.joinIntoString (" ")));
        }
        rep.line (states + "   (3 = valid, other = default)");
        if (details.isEmpty())
            rep.line ("  no active steps or notes in any pattern");
        for (const auto& d : details)
            rep.line (d);
    }

    const auto ms = juce::Time::getMillisecondCounterHiRes() - started;
    rep.line();
    rep.line ("Scales: 0 Chromatic, 1 Major, 2 Minor, 3 Dorian, 4 Mixolydian, 5 Harmonic minor, 6 Blues, 8 User 1, 9 User 2");
    rep.line (juce::String (requests) + " requests, " + juce::String (answered) + " answered, "
              + juce::String (ms / 1000.0, 1) + " s (" + juce::String (ms / juce::jmax (1, requests), 1) + " ms each)");

    if (outFile.replaceWithText (rep.text))
        std::cout << "Saved " << outFile.getFullPathName() << std::endl;
    return answered > 0 ? 0 : 1;
}
