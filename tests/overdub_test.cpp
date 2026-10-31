// overdub_test: Source/Model/Overdub.h (recording notes from MIDI clock + note-ons onto a pattern). No JUCE.
#include <cstdio>
#include "Model/Overdub.h"

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

using namespace ksp::model;

static bool send (Overdub& o, Pattern& p, std::uint8_t a, std::uint8_t b = 0, std::uint8_t c = 0)
{
    const std::uint8_t m[3] { a, b, c };
    return o.handle (p, m, a >= 0xF8 ? 1 : 3);
}

static void clocks (Overdub& o, Pattern& p, int n)
{
    for (int i = 0; i < n; ++i)
        send (o, p, 0xF8);
}

static bool has (const Pattern& p, int step, int pitch)
{
    for (const auto& n : p.steps[static_cast<std::size_t> (step)].notes)
        if (n.pitch == pitch)
            return true;
    return false;
}

int main()
{
    Pattern p;
    p.length = 16;
    p.defaults.gate = 50;
    p.steps[0].notes.push_back ({ 60, 64, 100, 0, 0 });
    p.steps[0].on = true;
    Overdub o (6, 0);

    // 1. Before Start nothing is recorded
    CHECK (1, ! send (o, p, 0x90, 64, 90));

    // 2. After Start: the stored note on step 1 is skipped (the sequencer playing it), a new pitch is added
    send (o, p, 0xFA);
    clocks (o, p, 1);
    CHECK (2, ! send (o, p, 0x90, 60, 100));
    CHECK (2, send (o, p, 0x90, 67, 80) && has (p, 0, 67));
    const auto& n = p.steps[0].notes.back();
    CHECK (2, n.velocity == 80 && n.gate == 50);

    // 3. Nearest step: tick 8 -> step 1 (index 1), tick 9 -> step 2 (index 2)
    clocks (o, p, 7);   // tick 7
    send (o, p, 0xF8);  // tick 8
    CHECK (3, send (o, p, 0x90, 62, 90) && has (p, 1, 62));
    send (o, p, 0xF8);  // tick 9
    CHECK (3, send (o, p, 0x90, 62, 90) && has (p, 2, 62));
    CHECK (3, p.steps[2].on);

    // 4. Wrong channel, velocity 0 (note-off) and note-off messages are ignored
    CHECK (4, ! send (o, p, 0x91, 70, 90));
    CHECK (4, ! send (o, p, 0x90, 71, 0));
    CHECK (4, ! send (o, p, 0x80, 72, 0));

    // 5. Just before the loop ends it rounds to the first step (tick 94 of 96)
    clocks (o, p, 85);   // tick 94
    CHECK (5, o.stepAt (16) == 0 && send (o, p, 0x90, 48, 90) && has (p, 0, 48));

    // 6. Stop pauses, Continue resumes from the same tick, Start goes back to 0
    send (o, p, 0xFC);
    CHECK (6, ! send (o, p, 0x90, 50, 90));
    send (o, p, 0xFB);
    CHECK (6, o.stepAt (16) == 0);
    send (o, p, 0xFA);
    clocks (o, p, 13);   // tick 12 -> index 2
    CHECK (6, o.stepAt (16) == 2);

    // 7. A full step refuses more notes and counts them
    Pattern q;
    Overdub r (6, 0);
    send (r, q, 0xFA);
    for (int k = 0; k < kMaxStepNotes; ++k)
        send (r, q, 0x90, static_cast<std::uint8_t> (40 + k), 90);
    CHECK (7, static_cast<int> (q.steps[0].notes.size()) == kMaxStepNotes && r.added == kMaxStepNotes);
    CHECK (7, ! send (r, q, 0x90, 100, 90) && r.dropped == 1);
    CHECK (7, check (q).empty());

    std::printf (failures == 0 ? "overdub_test: OK\n" : "overdub_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
