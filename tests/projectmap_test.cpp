// projectmap_test: Source/Model/ProjectMap.h (the whole project as one value). No JUCE.
#include <cstdio>
#include "Model/ProjectMap.h"

static int failures = 0;

#define CHECK(n, ...)                                                \
    do {                                                             \
        if (! (__VA_ARGS__)) { std::printf ("FAIL %d: %s\n", n, #__VA_ARGS__); ++failures; } \
    } while (0)

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
    using namespace ksp::model;
    auto p = blank();

    // track 1: pattern 2 with 3 notes (lengths 16 and 13 on two patterns), pattern 3 with one
    Pattern a;
    a.valid = true;
    a.length = 16;
    for (int i : { 0, 4, 8 })
    {
        a.steps[static_cast<std::size_t> (i)].on = true;
        a.steps[static_cast<std::size_t> (i)].notes.push_back ({ 60 + i, 64, 100, kShiftCentre, kRandomMax });
    }
    CHECK (1, writePattern (p, 1, 2, a).empty());
    Pattern b = a;
    b.length = 13;
    CHECK (1, writePattern (p, 1, 3, b).empty());

    // scene 1: T1 chain 2-3-2, the other tracks pattern 1; scene 2: T1 pattern 3
    Scene s1;
    s1.valid = true;
    s1.chain[0] = { 2, 3, 2 };
    CHECK (2, writeScene (p, 1, s1).empty());
    Scene s2;
    s2.valid = true;
    s2.pattern = { 3, 1, 1, 1 };
    CHECK (2, writeScene (p, 2, s2).empty());

    const auto m = buildMap (p);
    CHECK (3, m.savedScenes == 2 && m.usedPatterns == 2);
    CHECK (3, m.patterns[0][1].valid && m.patterns[0][2].valid && ! m.patterns[0][0].valid);

    const auto st = noteStats (m.patterns[0][1]);
    CHECK (4, st.count == 3 && st.lo == 60 && st.hi == 68);
    CHECK (4, noteStats (m.patterns[1][0]).count == 0);

    CHECK (5, playOrder (m, 1, 0) == std::vector<int> { 2, 3, 2 });
    CHECK (5, playOrder (m, 1, 1) == std::vector<int> { 1 });
    CHECK (5, playOrder (m, 2, 0) == std::vector<int> { 3 });
    CHECK (5, playOrder (m, 3, 0).empty());   // scene 3 is not saved

    CHECK (6, scenesUsing (m, 0, 2) == std::vector<int> { 1 });
    CHECK (6, scenesUsing (m, 0, 3) == std::vector<int> { 1, 2 });
    CHECK (6, scenesUsing (m, 0, 5).empty());

    CHECK (7, loopSteps (m, 1, 0) == 16 + 13 + 16 && loopSteps (m, 2, 0) == 13 && loopSteps (m, 5, 0) == 0);

    std::printf (failures == 0 ? "projectmap_test OK\n" : "projectmap_test FAILED\n");
    return failures == 0 ? 0 : 1;
}
