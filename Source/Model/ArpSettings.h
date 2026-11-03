/*
    Arpeggiator settings of tracks 2-4, decoded from the device (read-only reads after one change at a time, 2026-11-03,
    firmware 2.5.20; see docs/manual/05-hardware-log.md):
      - mode and octave: the track item (0x7C / 0x7D / 0x7E) param 100 [pattern 1..16], per pattern:
        value = 16 * (octave + 1) + 2 * mode, i.e. bits 1-3 = mode (Up 0, Down 1, Exclu 2, Inclu 3, Rand 4, Order 5, Poly 6),
        bits 4-6 = octave + 1 (octave -1..+3 -> 0..4), bit 0 not seen set (kept as it is);
      - swing: the project item 0x78 param 56 [track - 1] = percent - 50 (T2 = [1], T3 = [2]; T4 = [3] by the same rule, not
        tested on the device yet).
    Not decoded yet: the arp time division (one value, 0x78 p68) and the on/off flag (track item p86 bit 0x40).
    Plain std, no JUCE.
*/
#pragma once

#include <algorithm>
#include "Pattern.h"

namespace ksp::model
{

namespace ap
{
    inline constexpr std::uint8_t config = 100, swing = 56;
    inline constexpr int kModes = 7, kOctaveMin = -1, kOctaveMax = 3, kSwingMin = 50, kSwingMax = 75;
    inline constexpr const char* modeNames[kModes] { "Up", "Down", "Exclu", "Inclu", "Rand", "Order", "Poly" };
}

struct ArpSettings
{
    bool valid = false;
    int mode = 0;       // 0..6, see ap::modeNames
    int octave = 0;     // -1..+3
    int swing = 50;     // percent, 50..75
};

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
    a.valid = true;
    return a;
}

/** Writes mode and octave of the pattern and the swing of the track. Returns false when the project lacks those values. */
inline bool writeArp (ProjectData& p, int track, int pat, const ArpSettings& a)
{
    if (track < 2 || track > 4 || pat < 1 || pat > 16)
        return false;
    int* c = cell (p, item::track (track), ap::config, { static_cast<std::uint8_t> (pat) });
    int* s = cell (p, item::project, ap::swing, { static_cast<std::uint8_t> (track - 1) });
    if (c == nullptr || s == nullptr || *c == kUnreadable)
        return false;
    const int mode = std::clamp (a.mode, 0, ap::kModes - 1);
    const int oct = std::clamp (a.octave, ap::kOctaveMin, ap::kOctaveMax);
    *c = (*c & ~0x7E) | (mode << 1) | ((oct + 1) << 4);
    *s = std::clamp (a.swing, ap::kSwingMin, ap::kSwingMax) - 50;
    return true;
}

}
