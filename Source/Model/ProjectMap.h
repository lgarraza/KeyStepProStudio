/*
    ProjectMap.h
    The whole structure of a project in one value, for the Project Map window: the 16 scenes (a pattern or a chain per track) and the 16 patterns of
    each of the 4 sequencer tracks, read from a ProjectData without touching the device. Drums are not part of Pattern.h yet, so they are not here.
    C++17 + std, no JUCE.
*/

#pragma once

#include <algorithm>
#include <array>
#include <vector>
#include "Pattern.h"
#include "Scene.h"

namespace ksp::model
{

inline constexpr int kPatterns = 16;

struct ProjectMap
{
    std::array<Scene, kScenes> scenes {};                                  // index 0 = scene 1
    std::array<std::array<Pattern, kPatterns>, 4> patterns {};             // [track 0..3][pattern 0..15]
    int savedScenes = 0;
    int usedPatterns = 0;
};

inline ProjectMap buildMap (ProjectData& p)
{
    ProjectMap m;
    for (int s = 1; s <= kScenes; ++s)
    {
        m.scenes[static_cast<std::size_t> (s - 1)] = readScene (p, s);
        if (m.scenes[static_cast<std::size_t> (s - 1)].valid)
            ++m.savedScenes;
    }
    for (int t = 1; t <= 4; ++t)
        for (int k = 1; k <= kPatterns; ++k)
        {
            auto& pt = m.patterns[static_cast<std::size_t> (t - 1)][static_cast<std::size_t> (k - 1)];
            pt = readPattern (p, t, k);
            if (pt.valid)
                ++m.usedPatterns;
        }
    return m;
}

/** Notes of a pattern within its length; lo / hi are the lowest and highest pitch (both 0 when there are none). */
struct NoteStats { int count = 0, lo = 0, hi = 0; };

inline NoteStats noteStats (const Pattern& pt)
{
    NoteStats st;
    for (int i = 0; i < std::min (pt.length, kSteps); ++i)
    {
        const auto& s = pt.steps[static_cast<std::size_t> (i)];
        if (! s.on)
            continue;
        for (const auto& n : s.notes)
        {
            st.lo = st.count == 0 ? n.pitch : std::min (st.lo, n.pitch);
            st.hi = st.count == 0 ? n.pitch : std::max (st.hi, n.pitch);
            ++st.count;
        }
    }
    return st;
}

/** The patterns a track plays in a scene, in order: its chain, or its single pattern when it has no chain. Empty if the scene is not saved. */
inline std::vector<int> playOrder (const ProjectMap& m, int scene, int track)
{
    const auto& sc = m.scenes[static_cast<std::size_t> (scene - 1)];
    if (! sc.valid)
        return {};
    const auto& c = sc.chain[static_cast<std::size_t> (track)];
    return c.empty() ? std::vector<int> { sc.pattern[static_cast<std::size_t> (track)] } : c;
}

/** Saved scenes (1..16) that play pattern `pat` (1..16) of `track` (0..3), alone or inside a chain. */
inline std::vector<int> scenesUsing (const ProjectMap& m, int track, int pat)
{
    std::vector<int> out;
    for (int s = 1; s <= kScenes; ++s)
    {
        const auto order = playOrder (m, s, track);
        if (std::find (order.begin(), order.end(), pat) != order.end())
            out.push_back (s);
    }
    return out;
}

/** Total steps of a track's loop in a scene (the sum of its patterns' lengths). */
inline int loopSteps (const ProjectMap& m, int scene, int track)
{
    int total = 0;
    for (int pat : playOrder (m, scene, track))
        total += m.patterns[static_cast<std::size_t> (track)][static_cast<std::size_t> (pat - 1)].length;
    return total;
}

}
