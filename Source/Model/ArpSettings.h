/*
    Arpeggiator settings of tracks 2-4, decoded from the device (read-only reads after one change at a time, 2026-11-03,
    firmware 2.5.20; see docs/manual/05-hardware-log.md):
      - mode and octave: the track item (0x7C / 0x7D / 0x7E) param 100 [pattern 1..16], per pattern:
        value = 16 * (octave + 1) + 2 * mode, i.e. bits 1-3 = mode (Up 0, Down 1, Exclu 2, Inclu 3, Rand 4, Order 5, Poly 6),
        bits 4-6 = octave + 1 (octave -1..+3 -> 0..4), bit 0 not seen set (kept as it is);
      - swing: the project item 0x78 param 56 [track - 1] = percent - 50 (T2 = [1], T3 = [2]; T4 = [3] by the same rule, not
        tested on the device yet).
      - time division: per track, in the project item 0x78: p68 = div(T2) + 16 * div(T3), p69 = div(T4), code 0 = 1/4, 1 = 1/4T,
        2 = 1/8, 3 = 1/8T, 4 = 1/16, 5 = 1/16T, 6 = 1/32, 7 = 1/32T (T2 1/16T = 69, T3 1/8T = 52, T3 1/32T = 116, T4 1/8 = p69 2).
        The sequencer's division is a different thing (per pattern, Pattern.h config).
    On / off: bit 0x40 of the track item's param 86 [1] (the same bit as drum mode on track 1): T2-T4 1 = arp, 0 = sequencer; confirmed
    by turning T3 from arp to sequencer (66 -> 2).
    Plain std, no JUCE.
*/
#pragma once

#include <algorithm>
#include "Pattern.h"

namespace ksp::model
{

namespace ap
{
    inline constexpr std::uint8_t config = 100, swing = 56, division = 68, mode = 86;
    inline constexpr int kArpOnBit = 0x40;
    inline constexpr int kDivisions = 8;   // 1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 1/32T
    inline constexpr const char* divisionNames[kDivisions] { "1/4", "1/4 triplet", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32", "1/32 triplet" };
    inline constexpr const char* divisionShortNames[kDivisions] { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
    inline constexpr int kModes = 7, kOctaveMin = -1, kOctaveMax = 3, kSwingMin = 50, kSwingMax = 75;
    inline constexpr const char* modeNames[kModes] { "Up", "Down", "Exclu", "Inclu", "Rand", "Order", "Poly" };
}

struct ArpSettings
{
    bool valid = false;
    int mode = 0;       // 0..6, see ap::modeNames
    int octave = 0;     // -1..+3
    int swing = 50;     // percent, 50..75
    int division = 4;   // 0..7, see ap::divisionNames (4 = 1/16)
    bool on = false;    // the track plays its arpeggiator instead of the sequencer
};

/** The cell and shift holding a track's division: T2 low bits of p68, T3 bits 4-6 of p68, T4 p69. */
inline int* divisionCell (ProjectData& p, int track, int& shift)
{
    shift = track == 3 ? 4 : 0;
    return cell (p, item::project, static_cast<std::uint8_t> (ap::division + (track == 4 ? 1 : 0)), { 1 });
}

/** track 2..4, pattern 1..16. */
inline ArpSettings readArp (ProjectData& p, int track, int pat)
{
    ArpSettings a;
    if (track < 2 || track > 4 || pat < 1 || pat > 16)
        return a;
    const int* c = cell (p, item::track (track), ap::config, { static_cast<std::uint8_t> (pat) });
    const int* s = cell (p, item::project, ap::swing, { static_cast<std::uint8_t> (track - 1) });
    if (c == nullptr || *c == kUnreadable)
        return a;
    a.mode = std::clamp ((*c >> 1) & 7, 0, ap::kModes - 1);
    a.octave = std::clamp (((*c >> 4) & 7) - 1, ap::kOctaveMin, ap::kOctaveMax);
    a.swing = s != nullptr && *s != kUnreadable ? std::clamp (*s + 50, ap::kSwingMin, ap::kSwingMax) : 50;
    const int* m = cell (p, item::track (track), ap::mode, { 1 });
    a.on = m != nullptr && *m != kUnreadable && (*m & ap::kArpOnBit) != 0;
    int shift = 0;
    const int* d = divisionCell (p, track, shift);
    a.division = d != nullptr && *d != kUnreadable ? std::clamp ((*d >> shift) & 7, 0, ap::kDivisions - 1) : 4;
    a.valid = true;
    return a;
}

/** Turns the arpeggiator of track 2..4 on or off (off = the track plays its sequencer patterns). False when the project lacks the value. */
inline bool setArpOn (ProjectData& p, int track, bool on)
{
    if (track < 2 || track > 4)
        return false;
    int* m = cell (p, item::track (track), ap::mode, { 1 });
    if (m == nullptr || *m == kUnreadable)
        return false;
    *m = on ? (*m | ap::kArpOnBit) : (*m & ~ap::kArpOnBit);
    return true;
}

/** Writes mode and octave of the pattern and the swing, division and arp on/off of the track. Returns false when the project lacks those values. */
inline bool writeArp (ProjectData& p, int track, int pat, const ArpSettings& a)
{
    if (track < 2 || track > 4 || pat < 1 || pat > 16)
        return false;
    int* c = cell (p, item::track (track), ap::config, { static_cast<std::uint8_t> (pat) });
    int* s = cell (p, item::project, ap::swing, { static_cast<std::uint8_t> (track - 1) });
    int shift = 0;
    int* d = divisionCell (p, track, shift);
    int* m = cell (p, item::track (track), ap::mode, { 1 });
    if (c == nullptr || s == nullptr || d == nullptr || m == nullptr || *c == kUnreadable || *d == kUnreadable || *m == kUnreadable)
        return false;
    const int mode = std::clamp (a.mode, 0, ap::kModes - 1);
    const int oct = std::clamp (a.octave, ap::kOctaveMin, ap::kOctaveMax);
    *c = (*c & ~0x7E) | (mode << 1) | ((oct + 1) << 4);
    *s = std::clamp (a.swing, ap::kSwingMin, ap::kSwingMax) - 50;
    *d = (*d & ~(7 << shift)) | (std::clamp (a.division, 0, ap::kDivisions - 1) << shift);
    *m = a.on ? (*m | ap::kArpOnBit) : (*m & ~ap::kArpOnBit);
    return true;
}

}
