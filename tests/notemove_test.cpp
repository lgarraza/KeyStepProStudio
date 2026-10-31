// notemove_test: Source/Model/NoteMove.h (drag a group of notes in a pattern). No JUCE.
#include <cstdio>
#include "Model/NoteMove.h"

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

using namespace ksp::model;

static void put (Pattern& p, int step, int pitch, int velocity = 100)
{
    Note n;
    n.pitch = pitch;
    n.velocity = velocity;
    p.steps[static_cast<std::size_t> (step)].notes.push_back (n);
    p.steps[static_cast<std::size_t> (step)].on = true;
}

static int count (const Pattern& p, int step) { return static_cast<int> (p.steps[static_cast<std::size_t> (step)].notes.size()); }

int main()
{
    // 1. move one note: values travel, the source step turns off
    Pattern p;
    put (p, 2, 60, 77);
    std::vector<NoteRef> moved;
    CHECK (1, (moveNotes (p, { { 2, 60 } }, 3, 2, false, &moved)));
    CHECK (2, count (p, 2) == 0 && ! p.steps[2].on);
    CHECK (3, count (p, 5) == 1 && p.steps[5].on && p.steps[5].notes[0].pitch == 62 && p.steps[5].notes[0].velocity == 77);
    CHECK (4, (moved.size() == 1 && moved[0] == NoteRef { 5, 62 }));

    // 2. a group moves together, even onto its own old cells
    Pattern g;
    put (g, 0, 60);
    put (g, 1, 64);
    CHECK (5, (moveNotes (g, { { 0, 60 }, { 1, 64 } }, 1, 0, false)));
    CHECK (6, count (g, 0) == 0 && g.steps[1].notes[0].pitch == 60 && g.steps[2].notes[0].pitch == 64);

    // 3. out of range: nothing changes
    Pattern o;
    put (o, 0, 60);
    put (o, 63, 60);
    const auto before = o;
    CHECK (7, (! moveNotes (o, { { 0, 60 }, { 63, 60 } }, 1, 0, false)));
    CHECK (8, sameSteps (o, before));
    CHECK (9, (! moveNotes (o, { { 0, 60 } }, -1, 0, false)));
    CHECK (10, (! moveNotes (o, { { 0, 60 } }, 0, 68, false)));
    CHECK (11, (! moveNotes (o, { { 0, 60 } }, 0, -61, false)));

    // 4. same pitch at the target is replaced
    Pattern r;
    put (r, 0, 60, 10);
    put (r, 4, 60, 99);
    CHECK (12, (moveNotes (r, { { 0, 60 } }, 4, 0, false)));
    CHECK (13, count (r, 4) == 1 && r.steps[4].notes[0].velocity == 10);

    // 5. a chord of 16 refuses another note, untouched
    Pattern f;
    for (int i = 0; i < kMaxStepNotes; ++i)
        put (f, 1, 40 + i);
    put (f, 0, 90);
    const auto fb = f;
    CHECK (14, (! moveNotes (f, { { 0, 90 } }, 1, 0, false)));
    CHECK (15, sameSteps (f, fb));
    CHECK (16, moveNotes (f, { { 0, 90 } }, 1, -50, false));   // 40 is already there: replaced, not added
    CHECK (17, count (f, 1) == kMaxStepNotes);

    // 6. copy keeps the source
    Pattern c;
    put (c, 3, 55);
    CHECK (18, (moveNotes (c, { { 3, 55 } }, 8, 0, true)));
    CHECK (19, count (c, 3) == 1 && count (c, 11) == 1);

    // 7. a reference to a note that is not there is skipped
    Pattern e;
    CHECK (20, (moveNotes (e, { { 0, 60 } }, 1, 0, false, &moved) && moved.empty()));

    // 8. sameSteps sees a changed value
    auto a = c, b = c;
    CHECK (21, sameSteps (a, b));
    b.steps[3].notes[0].gate += 1;
    CHECK (22, ! sameSteps (a, b));

    if (failures == 0)
        std::printf ("notemove_test: OK\n");
    return failures == 0 ? 0 : 1;
}
