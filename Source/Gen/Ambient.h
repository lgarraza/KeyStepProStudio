/*
    Ambient.h
    Experiment: a small ambient generator that makes KeyStep Pro patterns instead of MIDI files. Four tracks with
    different roles and different pattern lengths (16, 13, 17, 19 steps), so the tracks drift against each other
    when they loop. Four variations per track (patterns 1..4) over a slow chord movement (degrees I, VI, IV, V).

      track 1  drone: a long low note on step 1 and, half the time, a fifth in the middle
      track 2  pad: a triad on step 1 (and sometimes a seventh later)
      track 3  slow melody: a random walk over the scale on some steps
      track 4  bells: sparse high notes, now and then two together

    Deterministic: the same Params give the same notes on every platform (std::mt19937, no <random> distributions).
    Sequencer data only, written with Model/Pattern.h. C++17 + std, no JUCE.
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "Roles.h"
#include "../Model/Scene.h"

namespace ksp::gen
{


inline constexpr int kLengths[4] { 16, 13, 17, 19 };

/** Moods, the generator's starting points: a key, a scale and four extended chords. "classic" is the
    original generator (D Dorian, triads); an unknown name gives classic too. */
inline Params moodParams (const std::string& name, unsigned seed = 1)
{
    Params p;
    p.seed = seed;
    if (name == "ethereal")   // C major: Cmaj7sus4, G7sus4, Am9, Fmaj9
    {
        p.root = 0;
        p.scale = 1;
        p.progression = { { 0, { 0, 3, 4, 6 } }, { 4, { 0, 3, 4, 6 } }, { 5, { 0, 2, 4, 6, 8 } }, { 3, { 0, 2, 4, 6, 8 } } };
    }
    else if (name == "dark")   // D minor: Dm9, Bbmaj7, Gm7, Asus4 (with the seventh)
    {
        p.root = 2;
        p.scale = 2;
        p.progression = { { 0, { 0, 2, 4, 6, 8 } }, { 5, { 0, 2, 4, 6 } }, { 3, { 0, 2, 4, 6 } }, { 4, { 0, 3, 4, 6 } } };
    }
    else if (name == "meditative")   // G mixolydian: open sus2 and add9 chords
    {
        p.root = 7;
        p.scale = 4;
        p.progression = { { 0, { 0, 1, 4 } }, { 6, { 0, 1, 4 } }, { 3, { 0, 2, 4, 8 } }, { 0, { 0, 3, 4 } } };
    }
    return p;
}


/** The ambient preset: four roles with different lengths, so the tracks drift against each other when they loop. */
inline RoleSpec ambientRole (int track)
{
    static const Role roles[4] { Role::drone, Role::pad, Role::melody, Role::bells };
    return { roles[track - 1], kLengths[track - 1], -1, -1, static_cast<unsigned> (track * 10) };
}

/** track 1..4, variation 0..3. `defaults` carries the pattern's shift and random as the device has them. */
inline model::Pattern makePattern (const Params& p, int track, int variation, const model::Note& defaults)
{
    return makeRolePattern (p, ambientRole (track), variation, defaults);
}

/** Writes patterns 1..variations of tracks first..last into the project (a ProjectData read from the device or a
    file). Returns an error (the project may be partly changed) or an empty string. */
inline std::string apply (ProjectData& project, const Params& p, int firstTrack = 1, int lastTrack = 4)
{
    if (p.variations < 1 || p.variations > 16)
        return "variations must be 1-16";
    if (firstTrack <= 1 && lastTrack >= 1)
        model::setDrumMode (project, false);   // track 1 plays notes here: a project left in drum mode by a style would hide them
    for (int t = firstTrack; t <= lastTrack; ++t)
        for (int v = 0; v < p.variations; ++v)
        {
            const auto base = model::readPattern (project, t, v + 1);
            auto pt = makePattern (p, t, v, base.defaults);
            pt.config = base.config;   // the playback settings the pattern has on the device
            if (p.slow)
            {
                model::setDivision (pt, t == 3 ? model::Division::eighth : model::Division::quarter);
                if (t == 2)
                    model::setPoly (pt, true);
            }
            if (auto e = model::writePattern (project, t, v + 1, pt); ! e.empty())
                return "track " + std::to_string (t) + " pattern " + std::to_string (v + 1) + ": " + e;
        }
    // a slot that held no project has its project state at 2 (default); the device turns it into 3 (valid) at the commit
    if (int* state = cell (project, item::project, 37, { 1 }))
        *state = 3;
    return {};
}

/** Scenes: 1..V play variation s on every track (the harmony of the four tracks agrees); with `cross`, V+1..2V keep the
    drone and the pad on variation s and move the melody and the bells one variation on (they cross the chord on purpose). */
inline std::string applyScenes (ProjectData& project, const Params& p, bool cross = true)
{
    const int v = p.variations;
    for (int s = 0; s < std::min (cross ? 2 * v : v, model::kScenes); ++s)
    {
        model::Scene sc;
        sc.valid = true;
        const int low = s % v + 1, high = s < v ? low : (s + 1) % v + 1;
        sc.pattern = { low, low, high, high };
        if (p.chains)
            for (std::size_t t = 0; t < 4; ++t)
                for (int k = 0; k < v; ++k)
                    sc.chain[t].push_back ((sc.pattern[t] - 1 + k) % v + 1);
        if (auto e = model::writeScene (project, s + 1, sc); ! e.empty())
            return "scene " + std::to_string (s + 1) + ": " + e;
    }
    return {};
}

/** One line per track: the steps of the pattern, "." for an empty step, otherwise the lowest note (+n when a chord). */
inline std::string preview (const model::Pattern& pt)
{
    std::string out;
    for (int s = 0; s < pt.length; ++s)
    {
        const auto& n = pt.steps[static_cast<std::size_t> (s)].notes;
        std::string cell = ".";
        if (! n.empty())
        {
            int lo = 127;
            for (const auto& x : n)
                lo = std::min (lo, x.pitch);
            cell = ksp::noteName (lo) + (n.size() > 1 ? "+" + std::to_string (n.size() - 1) : "");
        }
        out += cell + (s + 1 < pt.length ? " " : "");
    }
    return out;
}

} // namespace ksp::gen
