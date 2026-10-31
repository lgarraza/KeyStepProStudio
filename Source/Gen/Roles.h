/*
    Roles.h
    The role engine: a track's job (drone, pad, melody, bells, bass) as a small set of parameters, so a style is data and not
    code. A RoleSpec says what the track plays; makeRolePattern() turns it into a Pattern over the chord of the variation.
    Drums have their own pattern type (Model/DrumPattern.h): makeDrumPattern() builds them from a DrumSpec, euclidean.
    Ambient.h is the first preset (its four tracks are four RoleSpecs, same notes as before). Deterministic. C++17 + std, no JUCE.
*/

#pragma once

#include "Theory.h"
#include "../Model/DrumPattern.h"

namespace ksp::gen
{

enum class Role { drone, pad, melody, bells, bass };

struct RoleSpec
{
    Role role = Role::pad;
    int length = 16;        // steps, 1..64
    int density = -1;       // % of steps that play (melody, bells, bass); -1 = the role's own
    int low = -1;           // lowest pitch of the role's register (the root at or above it); -1 = the role's own
    unsigned salt = 0;      // RNG stream: the seed is p.seed * 1000 + salt + variation, so tracks differ
};

inline int roleLow (const RoleSpec& s)
{
    if (s.low >= 0)
        return s.low;
    switch (s.role)
    {
        case Role::drone: return 36;
        case Role::bass:  return 36;
        case Role::bells: return 72;
        default:          return 60;
    }
}

inline int roleDensity (const RoleSpec& s)
{
    if (s.density >= 0)
        return s.density;
    return s.role == Role::melody ? 40 : s.role == Role::bells ? 14 : s.role == Role::bass ? 50 : 100;
}

/** Euclidean rhythm: `pulses` hits spread over `steps`, rotated by `rotation`. */
inline std::vector<bool> euclid (int pulses, int steps, int rotation = 0)
{
    std::vector<bool> out (static_cast<std::size_t> (std::max (steps, 0)), false);
    if (steps <= 0)
        return out;
    pulses = std::clamp (pulses, 0, steps);
    for (int i = 0; i < pulses; ++i)
    {
        const int pos = (i * steps / pulses + rotation) % steps;
        out[static_cast<std::size_t> ((pos + steps) % steps)] = true;
    }
    return out;
}

/** The pattern a role plays for variation `variation` (0-based). `defaults` carries the pattern's shift and random. */
inline model::Pattern makeRolePattern (const Params& p, const RoleSpec& spec, int variation, const model::Note& defaults)
{
    using namespace detail;
    Rng r (p.seed * 1000u + spec.salt + static_cast<unsigned> (variation));
    const int len = spec.length;
    const int low = roleLow (spec), density = roleDensity (spec);
    const bool rich = ! p.progression.empty();
    const Chord chord = rich ? p.progression[static_cast<std::size_t> (variation) % p.progression.size()]
                             : Chord { kChordDegrees[variation % 4], { 0, 2, 4 } };
    const int deg = chord.degree;

    model::Pattern pt;
    pt.valid = true;
    pt.length = len;
    pt.root = p.root;
    pt.scale = p.scale;
    pt.defaults = defaults;

    switch (spec.role)
    {
        case Role::drone:   // a long low note on step 1 and, half the time, a fifth in the middle
            add (pt, 0, degreePitch (p, deg, low), 120, r.between (70, 90), defaults);
            if (r.chance (50))
                add (pt, len / 2, degreePitch (p, deg + 4, low), 110, r.between (55, 75), defaults);
            break;
        case Role::pad:     // the chord on step 1 (and sometimes a seventh later)
            for (int off : chord.offsets)
                add (pt, 0, degreePitch (p, deg + off, low), 120, r.between (50, 70), defaults);
            if (! rich && r.chance (40))
                add (pt, len / 2 + 1, degreePitch (p, deg + 6, low), 100, r.between (40, 60), defaults);
            break;
        case Role::melody:  // random walk, steps of one or two degrees
        {
            int d = deg + 7 + r.below (3);
            for (int s = 0; s < len; ++s)
                if (r.chance (density))
                {
                    d = std::clamp (d + r.between (-2, 2), 4, 14);
                    if (rich && r.chance (60))   // lean on the chord: the nearest chord tone
                    {
                        int best = d;
                        for (int off : chord.offsets)
                            for (int k = -2; k <= 2; ++k)
                            {
                                const int c = deg + off + 7 * k;
                                if (c >= 4 && c <= 14 && (best == d || std::abs (c - d) < std::abs (best - d)))
                                    best = c;
                            }
                        d = best;
                    }
                    add (pt, s, degreePitch (p, d, low), r.between (50, 90), r.between (45, 85), defaults);
                }
            break;
        }
        case Role::bells:   // sparse high notes, now and then two together
            for (int s = 0; s < len; ++s)
                if (r.chance (density))
                {
                    const int d = rich ? 14 + ((deg + chord.offsets[static_cast<std::size_t> (r.below (static_cast<int> (chord.offsets.size())))]) % 7 + 7) % 7 % 5
                                       : 14 + r.below (5);
                    add (pt, s, degreePitch (p, d, low), r.between (25, 40), r.between (35, 75), defaults);
                    if (r.chance (20))
                        add (pt, s, degreePitch (p, d + 2, low), 30, r.between (30, 60), defaults);
                }
            break;
        case Role::bass:    // the chord root on step 1 and on a euclidean spread of steps, the fifth now and then
        {
            const auto hits = euclid (std::max (1, len * density / 400), len, r.below (len));
            for (int s = 0; s < len; ++s)
                if (s == 0 || hits[static_cast<std::size_t> (s)])
                    add (pt, s, degreePitch (p, s != 0 && r.chance (25) ? deg + 4 : deg, low), r.between (40, 70),
                         s == 0 ? r.between (95, 115) : r.between (70, 95), defaults);
            break;
        }
    }
    return pt;
}

/** A drum groove: a kick on a euclidean spread, the snare on the backbeat, hats every n-th step. Lanes are the drum map's
    lanes (kick 0, snare 1, closed hat 2, open hat 3), whatever notes the map gives them. */
struct DrumSpec
{
    unsigned salt = 0;
    int length = 16;          // steps, 1..64
    int kickPulses = 4;       // euclidean kick hits in the pattern; 4 over 16 = four on the floor
    int kickRotation = 0;
    bool backbeat = true;     // snare on the 2nd and 4th beat (a quarter and three quarters of the pattern)
    int hatEvery = 2;         // a hat every n-th step (1 = all), 0 = none
    int hatOffset = 0;        // first hat step
    int openHatChance = 0;    // % of the hats that are open (lane 3)
    int ghostChance = 0;      // % of the steps that get a soft snare
    int swingShift = 0;       // time shift added to the odd steps (as the knob: 49 = on the step)
};

inline model::DrumPattern makeDrumPattern (const Params& p, const DrumSpec& spec, int variation)
{
    using namespace detail;
    Rng r (p.seed * 1000u + spec.salt + static_cast<unsigned> (variation));
    const int len = spec.length;
    model::DrumPattern d;
    d.valid = true;
    d.length = len;
    d.laneLength.fill (len);
    auto hit = [&] (int lane, int step, int gate, int vel, int shiftAdd = 0) {
        for (const auto& h : d.hits)
            if (h.lane == lane && h.step == step)
                return;
        model::Hit h;
        h.lane = lane;
        h.step = step;
        h.gate = std::clamp (gate, 1, 127);
        h.velocity = std::clamp (vel, 1, 127);
        h.shift = std::clamp (model::kShiftCentre + shiftAdd, 0, model::kShiftMax);
        d.hits.push_back (h);
    };
    const auto kick = euclid (spec.kickPulses, len, spec.kickRotation);
    for (int s = 0; s < len; ++s)
        if (kick[static_cast<std::size_t> (s)])
            hit (0, s, 64, s == 0 ? r.between (112, 127) : r.between (95, 115));
    if (spec.backbeat && len >= 4)
        for (int s = len / 4; s < len; s += len / 2)
            hit (1, s, 64, r.between (100, 120));
    if (spec.hatEvery > 0)
        for (int s = spec.hatOffset; s < len; s += spec.hatEvery)
            hit (spec.openHatChance > 0 && r.chance (spec.openHatChance) ? 3 : 2, s, 30,
                 s % 4 == 0 ? r.between (85, 100) : r.between (55, 80), s % 2 ? spec.swingShift : 0);
    if (spec.ghostChance > 0)
        for (int s = 0; s < len; ++s)
            if (r.chance (spec.ghostChance))
                hit (1, s, 30, r.between (30, 55));
    if (static_cast<int> (d.hits.size()) > model::kSlots)
        d.hits.resize (static_cast<std::size_t> (model::kSlots));
    return d;
}

} // namespace ksp::gen
