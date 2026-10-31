/*
    Pattern.h
    One sequencer pattern of a track (items 0x7B..0x7E) as plain data: length, root, scale, swing, steps with their
    notes. Read from and written back into a ProjectData (Backup.h) without touching the device; Restore.h's
    writePlan() then sends it. C++17 + std, no JUCE.

    Layout (KSCON's 6b-editor.js, MIT, from the firmware setter 0800d3c0 and MCC's recall plan): 192 note slots per
    pattern, params pitch / gate / velocity / time shift / randomness (0x6D..0x71) plus 0x32 = the 0-based step the slot
    belongs to (0x7F = unused), addressed [pattern, voice 1..3, slot 1..64]; at most 16 notes per step. Time shift is
    0..99 with 49 = on the step (-49%..+50% of a step); randomness is the chance the note plays, 0..100% (100 = always). Per step
    [pattern, 1, step]: on (0x30) and the pass mask (0x31, 4 bits, 15 = every pass; 0 mutes the step).
    Sequencer data only: drum lanes (track 1 in drum mode) and arpeggio settings are left as they are.
*/

#pragma once

#include <algorithm>
#include <array>
#include <stdexcept>
#include "../Ksp/Restore.h"

namespace ksp::model
{

namespace pp   // pattern params
{
    inline constexpr std::uint8_t swing = 0x61, length = 0x62, config = 0x63, on = 0x30, passes = 0x31, step = 0x32,
                                  root = param::seqRoot, scale = param::seqScale;
    inline constexpr std::uint8_t noteParams[5] { 0x6D, 0x6E, 0x6F, 0x70, 0x71 };   // pitch gate velocity shift random
}

inline constexpr int kSlots = 192, kMaxStepNotes = 16, kSteps = 64;
inline constexpr int kShiftCentre = 49, kShiftMax = 99, kRandomMax = 100;   // as stored (KSCON's editor)

struct Note
{
    int pitch = 60, gate = 64, velocity = 100, shift = kShiftCentre, random = kRandomMax;   // as stored
};

struct Step
{
    bool on = false;
    int passes = 15;
    std::vector<Note> notes;
};

struct Pattern
{
    bool valid = false;
    int length = 16;            // 1..64
    int root = 0;               // as stored
    int scale = 0;              // as stored (scaleName())
    int swing = 0;              // percent as shown (stored - 25); read only, writePattern leaves it as it is
    int config = 20;            // per-pattern playback settings (0x63), see Division / setDivision below; 20 = 1/16, Fwd, Poly
    std::array<Step, kSteps> steps {};
    Note defaults {};           // the pattern's default note values (gate, velocity ...), for new notes
    int unreadable = 0;         // slots the backup could not read: they are lost if the pattern is written back
};

inline std::vector<std::uint8_t> slotPath (int pat, int slot)
{
    return { static_cast<std::uint8_t> (pat), static_cast<std::uint8_t> (slot / 64 + 1), static_cast<std::uint8_t> (slot % 64 + 1) };
}

/** The playback settings that SHIFT + track shows on the KeyStep (Time Division, Seq Pattern, Mono/Poly), packed in
    `Pattern::config` (item 0x7B..0x7E, param 0x63, one per pattern). Decoded from four saves on the device (2026-10-29,
    base 20 = 1/16, Fwd, Poly): 1/4 -> 4, 1/8 -> 12, Mono -> 16, Walk -> 84. Known: bits 3-4 = division (0 = 1/4,
    1 = 1/8, 2 = 1/16), bit 2 = Poly, bit 6 = Walk. Not known: Rand, 1/32, triplet, bits 0-1, 5 and 7 (kept as they are). */
enum class Division { quarter = 0, eighth = 1, sixteenth = 2 };

inline void setDivision (Pattern& p, Division d) { p.config = (p.config & ~0x18) | (static_cast<int> (d) << 3); }
inline void setPoly (Pattern& p, bool poly) { p.config = poly ? (p.config | 0x04) : (p.config & ~0x04); }
inline bool isPoly (const Pattern& p) { return (p.config & 0x04) != 0; }
inline int divisionBits (const Pattern& p) { return (p.config >> 3) & 3; }

/** track 1..4, pattern 1..16. */
inline Pattern readPattern (ProjectData& p, int track, int pat)
{
    const auto it = item::track (track);
    const auto up = static_cast<std::uint8_t> (pat);
    auto get = [&] (std::uint8_t prm, const std::vector<std::uint8_t>& path, int fallback) {
        const int* v = cell (p, it, prm, path);
        return v == nullptr || *v == kUnreadable ? fallback : *v;
    };

    Pattern out;
    out.valid = get (param::patternState, { up }, 0) == 3;
    out.length = get (pp::length, { up }, 15) + 1;
    out.root = get (pp::root, { up }, 0);
    out.scale = get (pp::scale, { up }, 0);
    out.swing = std::max (0, get (pp::swing, { up }, 25) - 25);
    out.config = get (pp::config, { up }, 20);
    out.defaults = { get (pp::noteParams[0], { up }, 60), get (pp::noteParams[1], { up }, 64),
                     get (pp::noteParams[2], { up }, 100), get (pp::noteParams[3], { up }, kShiftCentre),
                     get (pp::noteParams[4], { up }, kRandomMax) };
    out.defaults.shift = std::clamp (out.defaults.shift, 0, kShiftMax);
    out.defaults.random = std::clamp (out.defaults.random, 0, kRandomMax);
    for (int i = 0; i < kSteps; ++i)
    {
        const auto s = static_cast<std::uint8_t> (i + 1);
        out.steps[static_cast<std::size_t> (i)].on = get (pp::on, { up, 1, s }, 0) != 0;
        out.steps[static_cast<std::size_t> (i)].passes = get (pp::passes, { up, 1, s }, 15);
    }
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const auto path = slotPath (pat, slot);
        const int* st = cell (p, it, pp::step, path);
        if (st == nullptr || *st == kUnreadable) { ++out.unreadable; continue; }
        if (*st == kEmpty || *st > 63)
            continue;
        std::array<int, 5> v {};
        bool ok = true;
        for (std::size_t k = 0; k < 5; ++k)
        {
            const int* c = cell (p, it, pp::noteParams[k], path);
            ok = ok && c != nullptr && *c != kUnreadable;
            v[k] = ok ? *c : 0;
        }
        if (! ok) { ++out.unreadable; continue; }
        // out-of-range shift / randomness (older versions of this app let the knobs reach 127) come back clamped
        out.steps[static_cast<std::size_t> (*st)].notes.push_back ({ v[0], v[1], v[2], std::clamp (v[3], 0, kShiftMax), std::clamp (v[4], 0, kRandomMax) });
    }
    return out;
}

/** Empty = fine; otherwise why `pt` cannot be written. */
inline std::string check (const Pattern& pt)
{
    int total = 0;
    for (std::size_t i = 0; i < pt.steps.size(); ++i)
    {
        const auto& s = pt.steps[i];
        total += static_cast<int> (s.notes.size());
        if (static_cast<int> (s.notes.size()) > kMaxStepNotes)
            return "step " + std::to_string (i + 1) + " has " + std::to_string (s.notes.size()) + " notes; the limit is 16";
        if (s.passes < 0 || s.passes > 15)
            return "step " + std::to_string (i + 1) + ": pass mask must be 0-15";
        for (const auto& n : s.notes)
        {
            for (int v : { n.pitch, n.gate, n.velocity })
                if (v < 0 || v > 127)
                    return "step " + std::to_string (i + 1) + ": a note value is outside 0-127";
            if (n.shift < 0 || n.shift > kShiftMax || n.random < 0 || n.random > kRandomMax)
                return "step " + std::to_string (i + 1) + ": time shift must be 0-99 and randomness 0-100";
        }
    }
    if (total > kSlots)
        return std::to_string (total) + " notes in the pattern; the KeyStep Pro holds 192";
    if (pt.length < 1 || pt.length > kSteps)
        return "pattern length is 1-64 steps";
    if (pt.root < 0 || pt.root > 127 || pt.scale < 0 || pt.scale > 127 || pt.config < 0 || pt.config > 127)
        return "root, scale or config out of range";
    return {};
}

/** Moves every note of `pt` by `semitones`. False (and `pt` unchanged) when a note would leave 0-127. */
inline bool transpose (Pattern& pt, int semitones)
{
    for (const auto& s : pt.steps)
        for (const auto& n : s.notes)
            if (n.pitch + semitones < 0 || n.pitch + semitones > 127)
                return false;
    for (auto& s : pt.steps)
        for (auto& n : s.notes)
            n.pitch += semitones;
    return true;
}

/** Writes `pt` into the project's values: notes go into slots in step order, the other slots are cleared, and the
    pattern becomes valid. Returns the error (nothing is changed) or an empty string. */
inline std::string writePattern (ProjectData& p, int track, int pat, const Pattern& pt)
{
    if (auto e = check (pt); ! e.empty())
        return e;
    const auto it = item::track (track);
    const auto up = static_cast<std::uint8_t> (pat);
    auto put = [&] (std::uint8_t prm, const std::vector<std::uint8_t>& path, int v) {
        int* c = cell (p, it, prm, path);
        if (c == nullptr)
            throw std::runtime_error ("no value " + std::to_string (it) + "/" + std::to_string (prm) + " in the project");
        *c = v;
    };

    put (param::patternState, { up }, 3);
    put (pp::length, { up }, pt.length - 1);
    put (pp::root, { up }, pt.root);
    put (pp::scale, { up }, pt.scale);
    put (pp::config, { up }, pt.config);
    for (std::size_t i = 0; i < pt.steps.size(); ++i)
    {
        const auto s = static_cast<std::uint8_t> (i + 1);
        put (pp::on, { up, 1, s }, pt.steps[i].on ? 1 : 0);
        put (pp::passes, { up, 1, s }, pt.steps[i].passes);
    }
    int slot = 0;
    for (std::size_t i = 0; i < pt.steps.size(); ++i)
        for (const auto& n : pt.steps[i].notes)
        {
            const auto path = slotPath (pat, slot++);
            put (pp::step, path, static_cast<int> (i));
            const int v[5] { n.pitch, n.gate, n.velocity, n.shift, n.random };
            for (std::size_t k = 0; k < 5; ++k)
                put (pp::noteParams[k], path, v[k]);
        }
    for (; slot < kSlots; ++slot)
    {
        const auto path = slotPath (pat, slot);
        put (pp::step, path, kEmpty);
        for (auto prm : pp::noteParams)
            put (prm, path, kEmpty);
    }
    return {};
}

/** Moves every valid pattern of `track` (1..4) by `semitones`; returns how many moved, or -1 (nothing changed) when a note
    would leave 0-127. */
inline int transposeTrack (ProjectData& p, int track, int semitones)
{
    std::vector<std::pair<int, Pattern>> moved;
    for (int pat = 1; pat <= 16; ++pat)
    {
        auto pt = readPattern (p, track, pat);
        if (! pt.valid)
            continue;
        if (! transpose (pt, semitones))
            return -1;
        moved.emplace_back (pat, std::move (pt));
    }
    for (const auto& m : moved)
        if (! writePattern (p, track, m.first, m.second).empty())
            return -1;
    return static_cast<int> (moved.size());
}

} // namespace ksp::model
