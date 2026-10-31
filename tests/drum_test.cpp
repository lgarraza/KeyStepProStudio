// drum_test: Source/Model/DrumPattern.h (read / write a drum pattern of track 1 inside a ProjectData). No JUCE.
#include <cstdio>
#include "Model/DrumPattern.h"

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

// A project as the device would send an untouched slot: every value 0, empty drum slots at 127.
static ksp::ProjectData blank()
{
    using namespace ksp;
    ProjectData p;
    p.project = 3;
    for (const auto& r : plan::expandPlan())
        p.data.push_back ({ r.item, r.param, r.path, std::vector<int> (r.count, 0) });
    for (int pat = 1; pat <= 16; ++pat)
        for (int s = 0; s < model::kSlots; ++s)
            *cell (p, item::track1, model::dp::step, model::slotPath (pat, s)) = kEmpty;
    return p;
}

int main()
{
    using namespace ksp;
    using namespace ksp::model;

    // 1. A blank pattern reads empty and not valid
    auto p = blank();
    auto d = readDrumPattern (p, 4);
    CHECK (1, ! d.valid && d.hits.empty() && d.unreadable == 0);

    // 2. Write a four-on-the-floor kick (lane 0), off-beat hats (lane 2) and a hit on lane 23 step 63
    d.length = 32;
    d.laneLength[2] = 16;
    for (int s = 0; s < 32; s += 4)
        d.hits.push_back ({ 0, s, 40, 110, 49, 100, 15 });
    for (int s = 2; s < 16; s += 4)
        d.hits.push_back ({ 2, s, 20, 70, 55, 60, 5 });
    d.hits.push_back ({ 23, 63, 64, 90, 0, 100, 15 });
    CHECK (2, writeDrumPattern (p, 4, d).empty());
    const auto back = readDrumPattern (p, 4);
    CHECK (2, back.valid && back.length == 32 && back.laneLength[2] == 16 && back.unreadable == 0);
    CHECK (2, back.hits.size() == 8 + 4 + 1);
    CHECK (2, back.hits[0].lane == 0 && back.hits[0].step == 0 && back.hits[0].velocity == 110 && back.hits[0].gate == 40);
    CHECK (2, back.hits[8].lane == 2 && back.hits[8].step == 2 && back.hits[8].passes == 5 && back.hits[8].shift == 55 && back.hits[8].random == 60);
    CHECK (2, back.hits.back().lane == 23 && back.hits.back().step == 63);
    CHECK (2, ! readDrumPattern (p, 5).valid);

    // 3. Lane masks: 7 steps per byte, index lane*10 + byte (kick on steps 0,4,..28: bytes 0..4)
    auto maskAt = [&] (int i) { return *cell (p, item::track1, dp::mask, slotPath (4, i)); };
    CHECK (3, maskAt (0) == (1 | 1 << 4));            // steps 0 and 4
    CHECK (3, maskAt (1) == (1 << 1 | 1 << 5));       // steps 8 and 12
    CHECK (3, maskAt (20) == (1 << 2 | 1 << 6));       // lane 2, steps 2 and 6 (byte 0)
    CHECK (3, maskAt (239) == (1 << 0));              // lane 23, step 63 = byte 9 bit 0
    CHECK (3, maskAt (5) == 0 && maskAt (30) == 0);

    // 4. Rewriting with fewer hits empties the freed slots (127 everywhere) and clears the masks
    d.hits.resize (3);
    CHECK (4, writeDrumPattern (p, 4, d).empty());
    CHECK (4, readDrumPattern (p, 4).hits.size() == 3);
    for (auto prm : { dp::step, dp::noteParams[0], dp::noteParams[1], dp::noteParams[2], dp::noteParams[3], dp::noteParams[4] })
        CHECK (4, *cell (p, item::track1, prm, slotPath (4, 3)) == kEmpty);
    CHECK (4, maskAt (239) == 0 && maskAt (20) == 0);
    CHECK (4, fixEmptyDrumSlots (p) == 0);   // nothing left for the restore fix to change

    // 5. Hits come back in lane then step order whatever order they were given in
    DrumPattern o;
    o.hits = { { 5, 9 }, { 1, 3 }, { 5, 2 } };
    CHECK (5, writeDrumPattern (p, 6, o).empty());
    const auto ord = readDrumPattern (p, 6);
    CHECK (5, ord.hits.size() == 3 && ord.hits[0].lane == 1 && ord.hits[1].step == 2 && ord.hits[2].step == 9);

    // 6. Limits are refused and change nothing
    const auto before = p.data;
    DrumPattern bad;
    bad.hits.assign (193, Hit {});
    for (std::size_t i = 0; i < bad.hits.size(); ++i)
        bad.hits[i].lane = static_cast<int> (i % 24), bad.hits[i].step = static_cast<int> (i / 24);
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());
    bad = {}; bad.hits = { { 0, 0 }, { 0, 0 } };
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());          // two hits on one lane step
    bad = {}; bad.hits = { { 24, 0 } };
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());
    bad = {}; bad.hits = { { 0, 64 } };
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());
    bad = {}; bad.hits = { { 0, 0, 128 } };
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());
    bad = {}; bad.length = 65;
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());
    bad = {}; bad.laneLength[3] = 0;
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());
    bad = {}; bad.hits = { { 0, 0, 64, 100, 100, 100, 15 } };
    CHECK (6, ! writeDrumPattern (p, 7, bad).empty());          // shift 100 > 99
    bool same = before.size() == p.data.size();
    for (std::size_t n = 0; same && n < before.size(); ++n)
        same = before[n].values == p.data[n].values;
    CHECK (6, same);

    // 7. Unreadable slots are counted, not read as hits
    *cell (p, item::track1, dp::step, slotPath (6, 0)) = kUnreadable;
    *cell (p, item::track1, dp::noteParams[0], slotPath (6, 1)) = 30;   // lane out of range
    const auto unr = readDrumPattern (p, 6);
    CHECK (7, unr.unreadable == 2 && unr.hits.size() == 1);

    // 8. Drum mode is bit 0x40 of 0x56 and the other bits stay
    *cell (p, item::track1, dp::mode, { 1 }) = 0x05;
    CHECK (8, ! isDrumMode (p) && setDrumMode (p, true) && isDrumMode (p) && *cell (p, item::track1, dp::mode, { 1 }) == 0x45);
    CHECK (8, setDrumMode (p, false) && ! isDrumMode (p) && *cell (p, item::track1, dp::mode, { 1 }) == 0x05);

    std::printf (failures == 0 ? "drum_test OK\n" : "drum_test: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
