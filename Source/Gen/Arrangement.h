/*
    Arrangement.h
    The "musical" arrangement of a style (Styles.h): eight scenes with an energy curve (intro, groove, drop, drop variation,
    breakdown, build, peak, outro) instead of the same energy everywhere.

    Patterns. Track 1 (drums) gets five patterns: 1 basic (kick and sparse hats), 2 groove (backbeat, open hats), 3 full (ghost
    notes), 4 full with a snare fill, 5 breakdown (no kick). Tracks 2-4 get twelve: pattern = level * 3 + chord + 1, with four
    levels (0 sparse, 1 groove, 2 full, 3 peak) and the first three chords of the style's progression (1-3 sparse, 4-6 groove,
    7-9 full, 10-12 peak). The level scales the role's density or pulses; the chord keeps the harmony of the four tracks together.
    Pattern 16 of every track is empty (the "dummy"), to silence a track in a scene.

    Scenes. Every scene has one chord and, per track, a chain of four patterns (four bars) that moves between levels, so the
    energy changes inside the scene as well. The tables below are data per style family. The KeyStep does not advance scenes
    by itself: the scenes are a map to play live. Deterministic. C++17 + std, no JUCE.
*/

#pragma once

#include "Styles.h"

namespace ksp::gen
{

inline constexpr int kLevels = 4, kChords = 3, kDrumLevels = 5, kArrangementScenes = 8, kDummy = 16;

struct SceneRow
{
    const char* label;
    int chord;               // the chord (0-2) all the tracks play in this scene
    int level[4][4];         // per track (1-4), the level of each of the four chain entries
};

struct Arrangement
{
    std::string family;
    SceneRow rows[kArrangementScenes];
};

/** Track 1: 0 basic, 1 groove, 2 full, 3 full + fill, 4 breakdown. Tracks 2-4: -1 the dummy, 0 sparse, 1 groove, 2 full, 3 peak. */
inline const std::vector<Arrangement>& arrangements()
{
    static const std::vector<Arrangement> all {
        { "house", {
            { "Intro",            0, { { 0, 0, 0, 0 }, { -1, -1, 0, 0 }, { -1, -1, -1, -1 }, { -1, -1, -1, -1 } } },
            { "Groove",           0, { { 0, 0, 1, 1 }, { 1, 1, 1, 1 }, { 0, 0, 1, 1 }, { -1, -1, 0, 0 } } },
            { "Drop 1",           0, { { 1, 1, 2, 2 }, { 2, 2, 2, 2 }, { 2, 2, 2, 2 }, { 1, 1, 2, 2 } } },
            { "Drop 1 variation", 1, { { 2, 2, 2, 3 }, { 2, 3, 2, 3 }, { 2, 2, 2, 2 }, { 2, 3, 2, 3 } } },
            { "Breakdown",        2, { { 4, 4, 4, 4 }, { -1, -1, -1, -1 }, { 2, 2, 2, 2 }, { 2, 2, 2, 2 } } },
            { "Build",            1, { { 0, 1, 1, 2 }, { 1, 1, 2, 2 }, { 1, 1, 2, 2 }, { 1, 1, 2, 2 } } },
            { "Drop 2",           0, { { 2, 2, 3, 3 }, { 3, 3, 3, 3 }, { 3, 3, 3, 3 }, { 3, 3, 3, 3 } } },
            { "Outro",            0, { { 1, 1, 0, 0 }, { 1, 1, 0, 0 }, { 1, 0, 0, 0 }, { 0, 0, -1, -1 } } } } },
        { "techno", {
            { "Intro",            0, { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { -1, -1, -1, -1 }, { -1, -1, -1, -1 } } },
            { "Groove",           0, { { 0, 0, 0, 1 }, { 1, 1, 1, 1 }, { 0, 0, 0, 0 }, { -1, -1, 0, 0 } } },
            { "Development",      0, { { 1, 1, 1, 1 }, { 1, 1, 1, 2 }, { 1, 1, 1, 1 }, { 0, 0, 1, 1 } } },
            { "Development 2",    1, { { 1, 1, 1, 3 }, { 2, 2, 2, 2 }, { 1, 1, 2, 2 }, { 1, 1, 1, 1 } } },
            { "Breakdown",        2, { { 4, 4, 4, 4 }, { 0, 0, 0, 0 }, { 2, 2, 2, 2 }, { 1, 1, 1, 1 } } },
            { "Peak",             0, { { 2, 2, 2, 2 }, { 2, 2, 2, 2 }, { 2, 2, 2, 2 }, { 2, 2, 2, 2 } } },
            { "Peak 2",           1, { { 2, 2, 2, 3 }, { 3, 3, 3, 3 }, { 2, 3, 2, 3 }, { 3, 3, 3, 3 } } },
            { "Outro",            0, { { 1, 1, 0, 0 }, { 1, 1, 1, 0 }, { 0, 0, 0, 0 }, { 0, 0, -1, -1 } } } } },
        { "dnb", {
            { "Intro",            0, { { 0, 0, 0, 0 }, { -1, -1, 0, 0 }, { 1, 1, 1, 1 }, { -1, -1, -1, -1 } } },
            { "Build",            0, { { 0, 0, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 0, 0, 1, 1 } } },
            { "Drop 1",           0, { { 2, 2, 2, 3 }, { 2, 2, 2, 2 }, { 1, 1, 1, 1 }, { 1, 1, 2, 2 } } },
            { "Drop 1 variation", 1, { { 2, 3, 2, 3 }, { 2, 3, 2, 3 }, { 2, 2, 2, 2 }, { 2, 2, 3, 3 } } },
            { "Breakdown",        2, { { 4, 4, 4, 4 }, { 0, 0, 0, 0 }, { 2, 2, 2, 2 }, { 2, 2, 2, 2 } } },
            { "Build 2",          1, { { 0, 1, 1, 2 }, { 1, 2, 1, 2 }, { 1, 1, 2, 2 }, { 1, 1, 2, 2 } } },
            { "Drop 2",           0, { { 3, 3, 3, 3 }, { 3, 3, 3, 3 }, { 2, 2, 2, 2 }, { 3, 3, 3, 3 } } },
            { "Outro",            0, { { 1, 1, 0, 0 }, { 1, 1, 0, 0 }, { 1, 1, 0, 0 }, { 0, 0, -1, -1 } } } } },
    };
    return all;
}

/** The arrangement of the style's family (the house one when the family has none). */
inline const Arrangement& arrangementFor (const Style& s)
{
    for (const auto& a : arrangements())
        if (a.family == s.family)
            return a;
    return arrangements().front();
}

/** The pattern (1-12) of tracks 2-4 for an energy level (0-3) and a chord (0-2); a negative level is the empty pattern 16. */
inline int rolePatternIndex (int level, int chord) { return level < 0 ? kDummy : level * kChords + chord % kChords + 1; }

inline bool hasNotes (const model::Pattern& pt)
{
    for (const auto& st : pt.steps)
        if (st.on && ! st.notes.empty())
            return true;
    return false;
}

/** The role at an energy level: its density (melody, bells) or its pulses (bass, stabs) scaled, the style's own at level 2. */
inline RoleSpec levelSpec (const RoleSpec& s, int level)
{
    static const int percent[kLevels] { 35, 70, 100, 130 };
    const int pct = percent[std::clamp (level, 0, kLevels - 1)];
    RoleSpec o = s;
    o.density = std::clamp (roleDensity (s) * pct / 100, 1, 100);
    if (s.pulses >= 0)
        o.pulses = std::clamp ((s.pulses * pct + 50) / 100, 1, s.length);
    return o;
}

/** The drums at a level: 0 kick and sparse hats, 1 the groove, 2 with ghost notes, 3 with ghost notes and a fill, 4 no kick. */
inline DrumSpec drumLevelSpec (const DrumSpec& s, int level)
{
    DrumSpec o = s;
    switch (level)
    {
        case 0:
            o.backbeat = false;
            o.ghostChance = 0;
            o.fill = false;
            if (s.hatEvery > 0)
            {
                o.hatEvery = std::min (s.hatEvery * 2, 4);
                o.offbeatOpenHat = false;
            }
            break;
        case 1:
            o.ghostChance = 0;
            o.fill = false;
            break;
        case 2:
            o.fill = false;
            break;
        case 3:
            o.fill = true;
            break;
        default:
            o.kickPulses = 0;
            o.kickSteps.clear();
            o.ghostChance = 0;
            o.fill = false;
            break;
    }
    return o;
}

/** Writes the five drum patterns, the 12 patterns of tracks 2-4, the empty pattern 16 on the four tracks, scenes 1-8, the tempo and the project state. Returns an
    error (the project may be partly changed) or an empty string. */
inline std::string applyArrangement (ProjectData& project, const Style& s, const Params& p)
{
    if (! model::setDrumMode (project, true))
        return "this project has no drum mode value for track 1";
    for (int level = 0; level < kDrumLevels; ++level)
        if (auto e = model::writeDrumPattern (project, level + 1, makeDrumPattern (p, drumLevelSpec (s.drums, level), level)); ! e.empty())
            return "track 1 pattern " + std::to_string (level + 1) + ": " + e;
    for (int t = 2; t <= 4; ++t)
    {
        const auto& ts = s.tracks[t - 2];
        for (int level = 0; level < kLevels; ++level)
            for (int chord = 0; chord < kChords; ++chord)
            {
                const int index = rolePatternIndex (level, chord);
                const auto base = model::readPattern (project, t, index);
                auto spec = levelSpec (ts.spec, level);
                auto pt = makeRolePattern (p, spec, chord, base.defaults);
                for (int tries = 0; tries < 10 && ! hasNotes (pt); ++tries)   // a sparse pattern must not come out silent
                {
                    spec.density = std::min (100, roleDensity (spec) + 10);
                    pt = makeRolePattern (p, spec, chord, base.defaults);
                }
                pt.config = base.config;
                model::setDivision (pt, ts.division);
                model::setPoly (pt, ts.poly);
                if (auto e = model::writePattern (project, t, index, pt); ! e.empty())
                    return "track " + std::to_string (t) + " pattern " + std::to_string (index) + ": " + e;
            }
    }
    {   // the dummy: nothing plays
        model::DrumPattern none;
        none.valid = true;
        if (auto e = model::writeDrumPattern (project, kDummy, none); ! e.empty())
            return "track 1 pattern 16: " + e;
        for (int t = 2; t <= 4; ++t)
        {
            const auto base = model::readPattern (project, t, kDummy);
            model::Pattern pt;
            pt.valid = true;
            pt.length = s.tracks[t - 2].spec.length;
            pt.root = p.root;
            pt.scale = p.scale;
            pt.defaults = base.defaults;
            pt.config = base.config;
            model::setDivision (pt, s.tracks[t - 2].division);
            model::setPoly (pt, s.tracks[t - 2].poly);
            if (auto e = model::writePattern (project, t, kDummy, pt); ! e.empty())
                return "track " + std::to_string (t) + " pattern 16: " + e;
        }
    }
    const auto& arr = arrangementFor (s);
    for (int n = 0; n < kArrangementScenes; ++n)
    {
        const auto& row = arr.rows[n];
        model::Scene sc;
        sc.valid = true;
        for (int t = 0; t < 4; ++t)
            for (int k = 0; k < 4; ++k)
                sc.chain[static_cast<std::size_t> (t)].push_back (t == 0 ? row.level[t][k] + 1 : rolePatternIndex (row.level[t][k], row.chord));
        for (std::size_t t = 0; t < 4; ++t)
            sc.pattern[t] = sc.chain[t].front();
        if (auto e = model::writeScene (project, n + 1, sc); ! e.empty())
            return "scene " + std::to_string (n + 1) + ": " + e;
    }
    if (! setTempo (project, s.bpm))
        return "this project has no tempo value";
    if (int* state = cell (project, item::project, 37, { 1 }))
        *state = 3;
    return {};
}

} // namespace ksp::gen
