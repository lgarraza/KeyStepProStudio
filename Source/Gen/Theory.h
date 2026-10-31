/*
    Theory.h
    Shared pieces of the generators: the chord and parameter structs, the scales and the deterministic RNG (std::mt19937, no
    <random> distributions, so the same seed gives the same notes on every platform). C++17 + std, no JUCE.
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>
#include "../Model/Pattern.h"

namespace ksp::gen
{

/** A chord of the progression: the scale degree of its root and the scale degrees above it that sound (0 = root, 2 = third,
    4 = fifth, 6 = seventh, 8 = ninth, 3 = fourth for a sus4, 1 = second for a sus2). */
struct Chord
{
    int degree = 0;
    std::vector<int> offsets { 0, 2, 4 };
};

struct Params
{
    unsigned seed = 1;
    int root = 2;       // pitch class, 2 = D
    int scale = 3;      // as the KeyStep stores it: 1 Major, 2 Minor, 3 Dorian, 4 Mixolydian, 5 Harmonic minor
    int variations = 4; // patterns 1..variations of each track
    std::vector<Chord> progression;   // variation v plays chord v % size; empty = the classic triads on I VI IV V
    bool chains = false; // every scene also chains its patterns on each track (starting at the scene's pattern, all variations)
    bool slow = true;   // time division per track: drone, pad and bells 1/4, melody 1/8 (the pad is set to Poly)
};

inline const std::vector<int>& intervals (int scale)
{
    static const std::vector<int> major { 0, 2, 4, 5, 7, 9, 11 }, minor { 0, 2, 3, 5, 7, 8, 10 },
        dorian { 0, 2, 3, 5, 7, 9, 10 }, mixolydian { 0, 2, 4, 5, 7, 9, 10 }, harmonic { 0, 2, 3, 5, 7, 8, 11 };
    switch (scale)
    {
        case 1: return major;
        case 2: return minor;
        case 4: return mixolydian;
        case 5: return harmonic;
        default: return dorian;
    }
}

inline constexpr int kChordDegrees[4] { 0, 5, 3, 4 };

namespace detail
{
    struct Rng
    {
        explicit Rng (unsigned seed) : e (seed) {}
        int below (int n) { return static_cast<int> (e() % static_cast<unsigned> (n)); }
        int between (int lo, int hi) { return lo + below (hi - lo + 1); }
        bool chance (int percent) { return below (100) < percent; }
        std::mt19937 e;
    };

    /** Pitch of scale degree `degree` (may be negative or above 6) with the root's lowest occurrence at or above `low`. */
    inline int degreePitch (const Params& p, int degree, int low)
    {
        const auto& iv = intervals (p.scale);
        int base = low + ((p.root - low) % 12 + 12) % 12;   // the root at or above `low`
        const int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
        const int idx = degree - oct * 7;
        return base + 12 * oct + iv[static_cast<std::size_t> (idx)];
    }

    inline void add (model::Pattern& pt, int step, int pitch, int gate, int vel, const model::Note& d)
    {
        while (pitch > 108)   // a high root can push the top of the bells out of range
            pitch -= 12;
        auto& s = pt.steps[static_cast<std::size_t> (step)];
        if (static_cast<int> (s.notes.size()) >= model::kMaxStepNotes)
            return;
        model::Note n = d;
        n.pitch = std::clamp (pitch, 0, 127);
        n.gate = std::clamp (gate, 1, 127);
        n.velocity = std::clamp (vel, 1, 127);
        s.notes.push_back (n);
        s.on = true;
    }
}

} // namespace ksp::gen
