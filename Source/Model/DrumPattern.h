/*
    DrumPattern.h
    One drum pattern of track 1 in drum mode (item 0x7B) as plain data: length, a length per lane, and the hits (lane,
    step, gate, velocity, time shift, randomness, pass mask). Read from and written back into a ProjectData (Backup.h)
    without touching the device; Restore.h's writePlan() then sends it. C++17 + std, no JUCE.

    Layout (KSCON's 6b-editor.js, MIT, from the firmware setter 0800d900 and a beat saved on a KeyStep Pro, 2026-10-20):
    24 lanes. Per lane a 64-step on mask (0x34, index lane*10 + byte, 7 steps per byte) and a length (0x33,
    [pattern, 1, lane]); pattern length 0x73. 192 hit slots like the sequencer's, filled from slot 0 lane by lane:
    0x36 step (0-based), 0x75 lane, 0x76-0x79 gate / velocity / time shift / randomness, 0x35 pass mask. An empty slot
    holds 127 in every field (a slot with any other lane counts as used; with 192 of those every new hit says
    "192 notes limit"). Drum mode is bit 0x40 of 0x56 ([1]). Drum swing (0x72) and flags (0x74) are left as they are.
    Port, not yet verified against this app's own hardware runs: the read-back test on a slot 10-16 is still to do.
*/

#pragma once

#include <algorithm>
#include <array>
#include <vector>
#include "Pattern.h"

namespace ksp::model
{

namespace dp   // drum pattern params
{
    inline constexpr std::uint8_t length = 0x73, laneLength = 0x33, mask = 0x34, passes = 0x35, step = 0x36, mode = 0x56;
    inline constexpr std::uint8_t noteParams[5] { 0x75, 0x76, 0x77, 0x78, 0x79 };   // lane gate velocity shift random
}

inline constexpr int kDrumLanes = 24, kDrumModeBit = 0x40;

struct Hit
{
    int lane = 0, step = 0;     // lane 0..23, step 0..63
    int gate = 64, velocity = 100, shift = kShiftCentre, random = kRandomMax, passes = 15;   // as stored
};

struct DrumPattern
{
    bool valid = false;
    int length = 16;                                        // 1..64
    std::array<int, kDrumLanes> laneLength = [] { std::array<int, kDrumLanes> a {}; a.fill (16); return a; }();   // 1..64 per lane
    std::vector<Hit> hits;
    int unreadable = 0;         // slots the backup could not read: they are lost if the pattern is written back
};

/** Pattern 1..16 of track 1. */
inline DrumPattern readDrumPattern (ProjectData& p, int pat)
{
    const auto it = item::track1;
    const auto up = static_cast<std::uint8_t> (pat);
    auto get = [&] (std::uint8_t prm, const std::vector<std::uint8_t>& path, int fallback) {
        const int* v = cell (p, it, prm, path);
        return v == nullptr || *v == kUnreadable ? fallback : *v;
    };

    DrumPattern out;
    out.valid = get (param::patternState, { up }, 0) == 3;
    out.length = get (dp::length, { up }, 15) + 1;
    for (int l = 0; l < kDrumLanes; ++l)
        out.laneLength[static_cast<std::size_t> (l)] = get (dp::laneLength, { up, 1, static_cast<std::uint8_t> (l + 1) }, 15) + 1;
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const auto path = slotPath (pat, slot);
        const int* st = cell (p, it, dp::step, path);
        if (st == nullptr || *st == kUnreadable) { ++out.unreadable; continue; }
        if (*st == kEmpty || *st > 63)
            continue;
        std::array<int, 5> v {};
        bool ok = true;
        for (std::size_t k = 0; k < 5; ++k)
        {
            const int* c = cell (p, it, dp::noteParams[k], path);
            ok = ok && c != nullptr && *c != kUnreadable;
            v[k] = ok ? *c : 0;
        }
        if (! ok || v[0] >= kDrumLanes) { ++out.unreadable; continue; }
        out.hits.push_back ({ v[0], *st, v[1], v[2], std::clamp (v[3], 0, kShiftMax), std::clamp (v[4], 0, kRandomMax),
                              get (dp::passes, path, 15) });
    }
    return out;
}

/** Empty = fine; otherwise why `d` cannot be written. */
inline std::string check (const DrumPattern& d)
{
    if (static_cast<int> (d.hits.size()) > kSlots)
        return std::to_string (d.hits.size()) + " drum hits in the pattern; the KeyStep Pro holds 192";
    if (d.length < 1 || d.length > kSteps)
        return "pattern length is 1-64 steps";
    for (int n : d.laneLength)
        if (n < 1 || n > kSteps)
            return "a lane length is 1-64 steps";
    std::array<std::array<bool, kSteps>, kDrumLanes> seen {};
    for (const auto& h : d.hits)
    {
        if (h.lane < 0 || h.lane >= kDrumLanes || h.step < 0 || h.step >= kSteps)
            return "a drum hit is outside 24 lanes x 64 steps";
        if (seen[static_cast<std::size_t> (h.lane)][static_cast<std::size_t> (h.step)])
            return "lane " + std::to_string (h.lane + 1) + " has two hits on step " + std::to_string (h.step + 1);
        seen[static_cast<std::size_t> (h.lane)][static_cast<std::size_t> (h.step)] = true;
        for (int v : { h.gate, h.velocity })
            if (v < 0 || v > 127)
                return "a hit value is outside 0-127";
        if (h.shift < 0 || h.shift > kShiftMax || h.random < 0 || h.random > kRandomMax)
            return "time shift must be 0-99 and randomness 0-100";
        if (h.passes < 0 || h.passes > 15)
            return "pass mask must be 0-15";
    }
    return {};
}

/** Writes `d` into the project's values: hits go into slots lane by lane (then by step), the other slots are emptied
    (127 everywhere), the lane masks are rebuilt and the pattern becomes valid. Returns the error (nothing is changed)
    or an empty string. */
inline std::string writeDrumPattern (ProjectData& p, int pat, const DrumPattern& d)
{
    if (auto e = check (d); ! e.empty())
        return e;
    const auto it = item::track1;
    const auto up = static_cast<std::uint8_t> (pat);
    // every cell is looked up before the first write, so a project without them is left untouched
    auto need = [&] (std::uint8_t prm, const std::vector<std::uint8_t>& path) {
        int* c = cell (p, it, prm, path);
        if (c == nullptr)
            throw std::runtime_error ("no value " + std::to_string (it) + "/" + std::to_string (prm) + " in the project");
        return c;
    };

    auto hits = d.hits;
    std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.lane != b.lane ? a.lane < b.lane : a.step < b.step; });

    std::vector<std::pair<int*, int>> w;
    auto put = [&] (std::uint8_t prm, const std::vector<std::uint8_t>& path, int v) { w.emplace_back (need (prm, path), v); };
    put (param::patternState, { up }, 3);
    put (dp::length, { up }, d.length - 1);
    for (int l = 0; l < kDrumLanes; ++l)
        put (dp::laneLength, { up, 1, static_cast<std::uint8_t> (l + 1) }, d.laneLength[static_cast<std::size_t> (l)] - 1);
    std::array<int, kDrumLanes * 10> mask {};
    for (const auto& h : hits)
        mask[static_cast<std::size_t> (h.lane * 10 + h.step / 7)] |= 1 << (h.step % 7);
    for (std::size_t i = 0; i < mask.size(); ++i)
        put (dp::mask, slotPath (pat, static_cast<int> (i)), mask[i]);
    int slot = 0;
    for (const auto& h : hits)
    {
        const auto path = slotPath (pat, slot++);
        put (dp::step, path, h.step);
        put (dp::passes, path, h.passes);
        const int v[5] { h.lane, h.gate, h.velocity, h.shift, h.random };
        for (std::size_t k = 0; k < 5; ++k)
            put (dp::noteParams[k], path, v[k]);
    }
    for (; slot < kSlots; ++slot)
    {
        const auto path = slotPath (pat, slot);
        put (dp::step, path, kEmpty);
        put (dp::passes, path, 15);
        for (auto prm : dp::noteParams)
            put (prm, path, kEmpty);
    }
    for (auto& [cellPtr, v] : w)
        *cellPtr = v;
    return {};
}

/** Whether track 1 is in drum mode (bit 0x40 of 0x56). */
inline bool isDrumMode (ProjectData& p)
{
    const int* m = cell (p, item::track1, dp::mode, { 1 });
    return m != nullptr && *m != kUnreadable && (*m & kDrumModeBit) != 0;
}

/** Switches track 1 between drum and sequencer mode. False when the project has no such value. */
inline bool setDrumMode (ProjectData& p, bool on)
{
    int* m = cell (p, item::track1, dp::mode, { 1 });
    if (m == nullptr || *m == kUnreadable)
        return false;
    *m = on ? (*m | kDrumModeBit) : (*m & ~kDrumModeBit);
    return true;
}

} // namespace ksp::model
