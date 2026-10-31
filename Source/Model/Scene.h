/*
    Scene.h
    One scene of a project (item 0x79) as plain data: which pattern each of the 4 tracks plays. Read from and written
    into a ProjectData like Pattern.h. C++17 + std, no JUCE.

    Layout, decoded on the KeyStep Pro of Leo (firmware 2.5.20, see docs/manual/05-hardware-log.md; two scenes saved
    on the device and diffed against a clean read): per scene [1..16]
      param 0x26 (38)  state: 2 = default, 3 = saved scene
      param 0x4C (76)  bitfield with the scene's current track (KSCON's mcc-params.md says current track and chain state;
                       it read 3 with track 4 selected and 0 with track 1 selected); written as 0 here
      param 0x53 (83)  [scene, track 1..4] = the track's pattern, 0-based (0 = pattern 1); track index 5 stays 0. When
                       the track has a pattern chain the device had 32 added (bit 5): 0 -> 32 with a chain on track 1
                       (one observation, taken as "chain active")
      param 0x54 (84)  [scene, track 1..4, 1..16] = the track's pattern chain: patterns 0-based in order, 127 = unused
                       (chain 1-2-3-4 on track 1 of scene 1 read 0 1 2 3 then 127)
    Not touched: params 77-82 (scene mute / solo / drum mutes, 0 in saved scenes). Project item 0x78 param 75 is the current
    scene. A pattern chain made on the device and not saved inside a scene is live state: it is lost at power off.
*/

#pragma once

#include <array>
#include <stdexcept>
#include "../Ksp/Restore.h"

namespace ksp::model
{

namespace sp   // scene params
{
    inline constexpr std::uint8_t state = 0x26, flag = 0x4C, pattern = 0x53, chain = 0x54;
    inline constexpr int chainFlag = 32, chainLength = 16;
}

inline constexpr int kScenes = 16;

struct Scene
{
    bool valid = false;
    std::array<int, 4> pattern { 1, 1, 1, 1 };   // 1..16 per track
    std::array<std::vector<int>, 4> chain {};    // per track: patterns 1..16 played in order (up to 16); empty = no chain
};

/** scene 1..16. */
inline Scene readScene (ProjectData& p, int scene)
{
    const auto s = static_cast<std::uint8_t> (scene);
    auto get = [&] (std::uint8_t prm, const std::vector<std::uint8_t>& path, int fallback) {
        const int* v = cell (p, item::scene, prm, path);
        return v == nullptr || *v == kUnreadable ? fallback : *v;
    };
    Scene out;
    out.valid = get (sp::state, { s }, 0) == 3;
    for (int t = 1; t <= 4; ++t)
    {
        out.pattern[static_cast<std::size_t> (t - 1)] = (get (sp::pattern, { s, static_cast<std::uint8_t> (t) }, 0) & 0x1F) + 1;
        for (int k = 1; k <= sp::chainLength; ++k)
        {
            const int v = get (sp::chain, { s, static_cast<std::uint8_t> (t), static_cast<std::uint8_t> (k) }, 127);
            if (v > 15)
                break;
            out.chain[static_cast<std::size_t> (t - 1)].push_back (v + 1);
        }
    }
    return out;
}

/** Empty = fine; otherwise why `sc` cannot be written. */
inline std::string check (const Scene& sc)
{
    for (int pat : sc.pattern)
        if (pat < 1 || pat > 16)
            return "a scene's pattern is 1-16";
    for (const auto& c : sc.chain)
    {
        if (static_cast<int> (c.size()) > sp::chainLength)
            return "a pattern chain holds up to 16 patterns";
        for (int pat : c)
            if (pat < 1 || pat > 16)
                return "a chain's pattern is 1-16";
    }
    return {};
}

/** Writes `sc` into scene 1..16 of the project's values and marks it saved. Returns the error or an empty string. */
inline std::string writeScene (ProjectData& p, int scene, const Scene& sc)
{
    if (scene < 1 || scene > kScenes)
        return "scene is 1-16";
    if (auto e = check (sc); ! e.empty())
        return e;
    const auto s = static_cast<std::uint8_t> (scene);
    auto put = [&] (std::uint8_t prm, const std::vector<std::uint8_t>& path, int v) {
        int* c = cell (p, item::scene, prm, path);
        if (c == nullptr)
            throw std::runtime_error ("no scene value " + std::to_string (prm) + " in the project");
        *c = v;
    };
    put (sp::state, { s }, 3);
    put (sp::flag, { s }, 0);
    for (int t = 1; t <= 4; ++t)
    {
        const auto& c = sc.chain[static_cast<std::size_t> (t - 1)];
        put (sp::pattern, { s, static_cast<std::uint8_t> (t) },
             (sc.pattern[static_cast<std::size_t> (t - 1)] - 1) | (c.empty() ? 0 : sp::chainFlag));
        for (int k = 1; k <= sp::chainLength; ++k)
            put (sp::chain, { s, static_cast<std::uint8_t> (t), static_cast<std::uint8_t> (k) },
                 k <= static_cast<int> (c.size()) ? c[static_cast<std::size_t> (k - 1)] - 1 : 127);
    }
    return {};
}

/** Puts scene 1..16 back to the default (state 2, pattern 1 on every track, no chains). Returns the error or an empty string. */
inline std::string clearScene (ProjectData& p, int scene)
{
    if (scene < 1 || scene > kScenes)
        return "scene is 1-16";
    if (auto e = writeScene (p, scene, Scene {}); ! e.empty())
        return e;
    const auto s = static_cast<std::uint8_t> (scene);
    *cell (p, item::scene, sp::state, { s }) = 2;
    return {};
}

} // namespace ksp::model
