// Nudge, reverse and invert of a whole pattern (Source/Model/PatternOps.h).
#include <cstdio>
#include "../Source/Model/PatternOps.h"

using namespace ksp::model;

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void put (Pattern& p, int step, int pitch)
{
    Note n;
    n.pitch = pitch;
    p.steps[static_cast<std::size_t> (step)].notes.push_back (n);
    p.steps[static_cast<std::size_t> (step)].on = true;
}

int main()
{
    Pattern p;
    p.length = 8;
    put (p, 0, 60);
    put (p, 3, 64);
    put (p, 7, 67);

    nudgePattern (p, 1);   // 0->1, 3->4, 7->0 (wraps)
    CHECK (p.steps[1].on && p.steps[4].on && p.steps[0].on && ! p.steps[3].on);
    CHECK (p.steps[0].notes[0].pitch == 67);
    nudgePattern (p, -1);
    CHECK (p.steps[0].notes[0].pitch == 60 && p.steps[7].notes[0].pitch == 67);
    nudgePattern (p, 8);   // a whole turn changes nothing
    CHECK (p.steps[0].notes[0].pitch == 60 && p.steps[3].on);

    reversePattern (p);    // 0->7, 3->4, 7->0
    CHECK (p.steps[7].notes[0].pitch == 60 && p.steps[4].notes[0].pitch == 64 && p.steps[0].notes[0].pitch == 67);
    reversePattern (p);
    CHECK (p.steps[0].notes[0].pitch == 60 && p.steps[7].notes[0].pitch == 67);

    CHECK (invertPattern (p));   // 60..67 mirrored: 60<->67, 64<->63
    CHECK (p.steps[0].notes[0].pitch == 67 && p.steps[7].notes[0].pitch == 60 && p.steps[3].notes[0].pitch == 63);
    CHECK (invertPattern (p));
    CHECK (p.steps[0].notes[0].pitch == 60 && p.steps[3].notes[0].pitch == 64 && p.steps[7].notes[0].pitch == 67);

    Pattern empty;
    CHECK (! invertPattern (empty));

    std::printf (failures == 0 ? "patternops_test OK\n" : "patternops_test FAILED\n");
    return failures == 0 ? 0 : 1;
}
