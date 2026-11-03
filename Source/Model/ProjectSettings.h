// Project settings kept in the project item 0x78: tempo (params 0x46-0x48, BPM * 100 in 7-bit parts, low part first) and the global swing
// (param 0x4A = 74, percent, 50..75, 50 = straight). Read on Leo's KeyStep Pro (firmware 2.5.20, project 1): 24, 92, 0 -> 118.00 BPM; swing 50.
// Plain std, no JUCE. See docs/manual/05-hardware-log.md.
#pragma once

#include <algorithm>
#include <cmath>
#include "../Ksp/Restore.h"

namespace ksp::model
{

namespace prs
{
    inline constexpr std::uint8_t tempo0 = 0x46, swing = 0x4A;
    inline constexpr double kTempoMin = 30.0, kTempoMax = 300.0;
    inline constexpr int kSwingMin = 50, kSwingMax = 75;
}

struct ProjectSettings
{
    bool valid = false;
    double tempo = 120.0;
    int swing = 50;
};

inline ProjectSettings readSettings (ProjectData& p)
{
    ProjectSettings s;
    int part[3] {};
    for (int k = 0; k < 3; ++k)
    {
        const int* c = cell (p, item::project, static_cast<std::uint8_t> (prs::tempo0 + k), { 1 });
        if (c == nullptr || *c == kUnreadable)
            return s;
        part[k] = *c;
    }
    s.tempo = static_cast<double> (part[0] | (part[1] << 7) | (part[2] << 14)) / 100.0;
    const int* sw = cell (p, item::project, prs::swing, { 1 });
    s.swing = sw != nullptr && *sw != kUnreadable ? std::clamp (*sw, prs::kSwingMin, prs::kSwingMax) : 50;
    s.valid = true;
    return s;
}

/** Writes tempo (30..300 BPM) and swing (50..75 %) into the project's values. Returns false when the project lacks those values. */
inline bool writeSettings (ProjectData& p, double tempo, int swing)
{
    int* c[3];
    for (int k = 0; k < 3; ++k)
        if ((c[k] = cell (p, item::project, static_cast<std::uint8_t> (prs::tempo0 + k), { 1 })) == nullptr)
            return false;
    int* sw = cell (p, item::project, prs::swing, { 1 });
    if (sw == nullptr)
        return false;
    const int bpm100 = static_cast<int> (std::lround (std::clamp (tempo, prs::kTempoMin, prs::kTempoMax) * 100.0));
    for (int k = 0; k < 3; ++k)
        *c[k] = (bpm100 >> (7 * k)) & 0x7F;
    *sw = std::clamp (swing, prs::kSwingMin, prs::kSwingMax);
    return true;
}

}
