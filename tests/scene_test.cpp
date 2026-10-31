// scene_test: Source/Model/Scene.h (read / write a scene inside a ProjectData) and the generator's scenes. No JUCE.
#include <cstdio>
#include "Gen/Ambient.h"

static int failures = 0;

#define CHECK(n, ...)                                                \
    do {                                                             \
        if (! (__VA_ARGS__)) { std::printf ("FAIL %d: %s\n", n, #__VA_ARGS__); ++failures; } \
    } while (0)

// A project as the device sends one without scenes: scene state 2 (default), everything else 0.
static ksp::ProjectData blank()
{
    using namespace ksp;
    ProjectData p;
    p.project = 16;
    for (const auto& r : plan::expandPlan())
        p.data.push_back ({ r.item, r.param, r.path, std::vector<int> (r.count, 0) });
    for (std::uint8_t s = 1; s <= 16; ++s)
        *cell (p, item::scene, model::sp::state, { s }) = 2;
    return p;
}

int main()
{
    using namespace ksp;
    using namespace ksp::model;

    // 1. Scene 1 as saved on the KeyStep (T1 pattern 2, T2 3, T3 4, T4 1): state 3, flag 3, patterns 1 2 3 0
    auto p = blank();
    CHECK (1, ! readScene (p, 1).valid);
    CHECK (1, writeScene (p, 1, { true, { 2, 3, 4, 1 } }).empty());
    CHECK (1, *cell (p, item::scene, sp::state, { 1 }) == 3 && *cell (p, item::scene, sp::flag, { 1 }) == 0);
    CHECK (1, *cell (p, item::scene, sp::pattern, { 1, 1 }) == 1 && *cell (p, item::scene, sp::pattern, { 1, 3 }) == 3);
    CHECK (1, *cell (p, item::scene, sp::pattern, { 1, 4 }) == 0 && *cell (p, item::scene, sp::pattern, { 1, 5 }) == 0);
    const auto back = readScene (p, 1);
    CHECK (1, back.valid && back.pattern == std::array<int, 4> { 2, 3, 4, 1 });
    CHECK (1, ! readScene (p, 2).valid);

    // 1b. A pattern chain as saved on the device (T1 of scene 1, chain 1-2-3-4): the chain list 0 1 2 3 then 127, and the
    //     pattern value gets 32 added; read back, the chain is there and the pattern is read without the flag
    Scene chained;
    chained.valid = true;
    chained.chain[0] = { 1, 2, 3, 4 };
    CHECK (1, writeScene (p, 3, chained).empty());
    CHECK (1, *cell (p, item::scene, sp::pattern, { 3, 1 }) == 32 && *cell (p, item::scene, sp::pattern, { 3, 2 }) == 0);
    CHECK (1, *cell (p, item::scene, sp::chain, { 3, 1, 1 }) == 0 && *cell (p, item::scene, sp::chain, { 3, 1, 4 }) == 3);
    CHECK (1, *cell (p, item::scene, sp::chain, { 3, 1, 5 }) == 127 && *cell (p, item::scene, sp::chain, { 3, 2, 1 }) == 127);
    const auto rc = readScene (p, 3);
    CHECK (1, rc.pattern[0] == 1 && rc.chain[0] == std::vector<int> { 1, 2, 3, 4 } && rc.chain[1].empty());
    chained.chain[1].assign (17, 1);
    CHECK (1, ! writeScene (p, 4, chained).empty());
    chained.chain[1] = { 17 };
    CHECK (1, ! writeScene (p, 4, chained).empty());

    // 2. Limits
    CHECK (2, ! writeScene (p, 17, {}).empty() && ! writeScene (p, 0, {}).empty());
    CHECK (2, ! writeScene (p, 2, { true, { 0, 1, 1, 1 } }).empty() && ! writeScene (p, 2, { true, { 1, 1, 17, 1 } }).empty());
    CHECK (2, ! readScene (p, 2).valid);

    // 3. The generator's scenes: 8 for 4 variations, aligned first, then melody and bells one variation on
    gen::Params params;
    CHECK (3, gen::applyScenes (p, params).empty());
    CHECK (3, readScene (p, 1).pattern == std::array<int, 4> { 1, 1, 1, 1 });
    CHECK (3, readScene (p, 4).pattern == std::array<int, 4> { 4, 4, 4, 4 });
    CHECK (3, readScene (p, 5).pattern == std::array<int, 4> { 1, 1, 2, 2 });
    CHECK (3, readScene (p, 8).pattern == std::array<int, 4> { 4, 4, 1, 1 });
    CHECK (3, ! readScene (p, 9).valid);
    gen::Params chains = params;
    chains.chains = true;
    CHECK (3, gen::applyScenes (p, chains).empty());
    CHECK (3, readScene (p, 1).chain[0] == std::vector<int> { 1, 2, 3, 4 } && readScene (p, 2).chain[3] == std::vector<int> { 2, 3, 4, 1 });
    CHECK (3, readScene (p, 5).chain[2] == std::vector<int> { 2, 3, 4, 1 } && readScene (p, 5).chain[0] == std::vector<int> { 1, 2, 3, 4 });

    // 4. The scenes go through the restore plan (scene states are sent with the other states, last)
    const auto w = writePlan (p);
    CHECK (4, ! w.requests.empty());

    std::printf (failures == 0 ? "scene_test: OK\n" : "scene_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
