/*
    Whole-pattern operations on a sequencer Pattern: nudge (rotate the steps inside the pattern length), reverse (play it backwards)
    and invert (mirror the pitches). Plain std, no JUCE. Each is undone by itself: nudge by the opposite amount, reverse and invert
    by running them again (invert mirrors around lowest + highest pitch, which stays the same). Chord notes, gate, velocity, shift and
    randomness travel with their notes.
*/
#pragma once

#include <algorithm>
#include "Pattern.h"

namespace ksp::model
{

/** Moves every step `by` steps to the right (negative = left), wrapping inside the pattern length. */
inline void nudgePattern (Pattern& p, int by)
{
    const int len = std::clamp (p.length, 1, kSteps);
    by = ((by % len) + len) % len;
    if (by == 0)
        return;
    auto old = p.steps;
    for (int i = 0; i < len; ++i)
        p.steps[static_cast<std::size_t> ((i + by) % len)] = old[static_cast<std::size_t> (i)];
}

/** Plays the pattern backwards: step i goes to length - 1 - i. */
inline void reversePattern (Pattern& p)
{
    const int len = std::clamp (p.length, 1, kSteps);
    std::reverse (p.steps.begin(), p.steps.begin() + len);
}

/** Mirrors the pitches around the middle of the pattern's range (lowest + highest - pitch). Returns false when there are no notes. */
inline bool invertPattern (Pattern& p)
{
    int lo = 128, hi = -1;
    for (const auto& s : p.steps)
        for (const auto& n : s.notes)
        {
            lo = std::min (lo, n.pitch);
            hi = std::max (hi, n.pitch);
        }
    if (hi < 0)
        return false;
    for (auto& s : p.steps)
        for (auto& n : s.notes)
            n.pitch = lo + hi - n.pitch;
    return true;
}

}
