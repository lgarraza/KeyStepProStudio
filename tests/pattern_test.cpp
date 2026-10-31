// pattern_test: Source/Model/Pattern.h (read / edit / write a sequencer pattern inside a ProjectData). No JUCE.
#include <cstdio>
#include "Model/Pattern.h"

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

// A project as the device would send an untouched slot: every value 0, empty note slots at 127.
static ksp::ProjectData blank()
{
    using namespace ksp;
    ProjectData p;
    p.project = 3;
    for (const auto& r : plan::expandPlan())
        p.data.push_back ({ r.item, r.param, r.path, std::vector<int> (r.count, 0) });
    for (int t = 1; t <= 4; ++t)
        for (int pat = 1; pat <= 16; ++pat)
            for (int s = 0; s < model::kSlots; ++s)
            {
                const auto path = model::slotPath (pat, s);
                *cell (p, item::track (t), model::pp::step, path) = kEmpty;
            }
    return p;
}

int main()
{
    using namespace ksp;
    using namespace ksp::model;

    // 1. A blank pattern reads empty and not valid
    auto p = blank();
    auto pt = readPattern (p, 2, 5);
    CHECK (1, ! pt.valid && pt.unreadable == 0);
    int notes = 0;
    for (const auto& s : pt.steps)
        notes += static_cast<int> (s.notes.size());
    CHECK (1, notes == 0);

    // 2. Edit and write: a 3-note chord on step 1, a single note on step 9 and a 12-step length
    pt.length = 12;
    pt.root = 2;
    pt.scale = 3;
    pt.steps[0].on = true;
    pt.steps[0].notes = { { 60, 64, 100, 0, 0 }, { 64, 64, 90, 0, 0 }, { 67, 64, 80, 0, 0 } };
    pt.steps[8].on = true;
    pt.steps[8].notes = { { 72, 32, 110, 5, 7 } };
    CHECK (2, writePattern (p, 2, 5, pt).empty());
    const auto back = readPattern (p, 2, 5);
    CHECK (2, back.valid && back.length == 12 && back.root == 2 && back.scale == 3);
    CHECK (2, back.steps[0].on && back.steps[0].notes.size() == 3 && back.steps[0].notes[2].pitch == 67);
    CHECK (2, back.steps[8].notes.size() == 1 && back.steps[8].notes[0].shift == 5 && back.steps[8].notes[0].random == 7);
    CHECK (2, ! back.steps[1].on && back.steps[1].notes.empty());
    // other patterns and tracks untouched
    CHECK (2, ! readPattern (p, 2, 6).valid && ! readPattern (p, 1, 5).valid);

    // 2b. A new note sits on the step and always plays; stored values past the range come back clamped
    CHECK (2, Note {}.shift == 49 && Note {}.random == 100);
    *cell (p, item::track2, pp::noteParams[3], slotPath (5, 3)) = 127;
    *cell (p, item::track2, pp::noteParams[4], slotPath (5, 3)) = 127;
    const auto clamped = readPattern (p, 2, 5).steps[8].notes[0];
    CHECK (2, clamped.shift == kShiftMax && clamped.random == kRandomMax);
    CHECK (2, writePattern (p, 2, 5, readPattern (p, 2, 5)).empty());
    CHECK (2, writePattern (p, 2, 5, pt).empty());

    // 3. Rewriting with fewer notes clears the freed slots
    pt.steps[0].notes.resize (1);
    CHECK (3, writePattern (p, 2, 5, pt).empty());
    const auto again = readPattern (p, 2, 5);
    CHECK (3, again.steps[0].notes.size() == 1 && again.steps[8].notes.size() == 1);
    CHECK (3, *cell (p, item::track2, pp::step, slotPath (5, 2)) == kEmpty);

    // 4. Limits are refused and change nothing
    auto bad = pt;
    bad.steps[3].notes.assign (17, Note {});
    const auto before = p.data;
    CHECK (4, writePattern (p, 2, 5, bad).find ("limit is 16") != std::string::npos);
    bad = pt;
    bad.length = 65;
    CHECK (4, ! writePattern (p, 2, 5, bad).empty());
    bad = pt;
    bad.steps[0].notes[0].pitch = 200;
    CHECK (4, ! writePattern (p, 2, 5, bad).empty());
    bad = pt;
    bad.steps[0].notes[0].shift = kShiftMax + 1;   // time shift is 0-99, randomness 0-100
    CHECK (4, ! writePattern (p, 2, 5, bad).empty());
    bad = pt;
    bad.steps[0].notes[0].random = kRandomMax + 1;
    CHECK (4, ! writePattern (p, 2, 5, bad).empty());
    bool same = before.size() == p.data.size();
    for (std::size_t n = 0; same && n < before.size(); ++n)
        same = before[n].values == p.data[n].values;
    CHECK (4, same);

    // 5. 192 notes fit, 193 do not
    Pattern full;
    full.length = 64;
    for (int i = 0; i < 12; ++i)
        full.steps[static_cast<std::size_t> (i)].notes.assign (16, Note {});
    CHECK (5, writePattern (p, 3, 1, full).empty() && readPattern (p, 3, 1).unreadable == 0);
    full.steps[12].notes.assign (1, Note {});
    CHECK (5, writePattern (p, 3, 1, full).find ("192") != std::string::npos);

    // 6. An unreadable slot is counted, not turned into a note
    *cell (p, item::track3, pp::step, slotPath (1, 0)) = kUnreadable;
    CHECK (6, readPattern (p, 3, 1).unreadable == 1);

    // 6b. Playback settings: as decoded on the device (base 20 = 1/16, Fwd, Poly), the other bits are kept
    Pattern cfg;
    cfg.config = 20;
    setDivision (cfg, Division::quarter);
    CHECK (6, cfg.config == 4);
    setDivision (cfg, Division::eighth);
    CHECK (6, cfg.config == 12 && divisionBits (cfg) == 1);
    cfg.config = 20;
    setPoly (cfg, false);
    CHECK (6, cfg.config == 16 && ! isPoly (cfg));
    cfg.config = 84;
    setDivision (cfg, Division::sixteenth);
    CHECK (6, cfg.config == 84 && isPoly (cfg));
    Pattern one;
    one.config = 84;
    CHECK (6, writePattern (p, 2, 7, one).empty() && readPattern (p, 2, 7).config == 84);

    // 6c. Transposing: every note moves, the range is checked, only valid patterns of the track move
    {
        auto t = blank();
        Pattern a;
        a.length = 8;
        a.steps[0].on = true;
        a.steps[0].notes = { { 60, 64, 100, 0, 0 }, { 67, 64, 100, 0, 0 } };
        CHECK (6, writePattern (t, 2, 1, a).empty() && writePattern (t, 2, 3, a).empty());
        CHECK (6, transposeTrack (t, 2, 12) == 2);
        CHECK (6, readPattern (t, 2, 1).steps[0].notes[0].pitch == 72 && readPattern (t, 2, 3).steps[0].notes[1].pitch == 79);
        CHECK (6, ! readPattern (t, 2, 2).valid && ! readPattern (t, 3, 1).valid);
        CHECK (6, transposeTrack (t, 2, 60) == -1 && readPattern (t, 2, 1).steps[0].notes[0].pitch == 72);   // 79 + 60 > 127
        CHECK (6, transposeTrack (t, 2, -12) == 2 && readPattern (t, 2, 1).steps[0].notes[0].pitch == 60);
        CHECK (6, transposeTrack (t, 4, 12) == 0);
    }

    // 6d. A write plan against the slot as it was sends only the blocks that changed, plus every state
    {
        auto before = blank();
        auto after = before;
        Pattern a;
        a.length = 4;
        a.steps[0].on = true;
        a.steps[0].notes = { { 64, 64, 100, 0, 0 } };
        CHECK (6, writePattern (after, 3, 2, a).empty());
        auto copyAfter = after;
        const auto full = writePlan (copyAfter);
        const auto diff = writePlan (after, &before);
        CHECK (6, ! diff.requests.empty() && diff.requests.size() < 400 && diff.requests.size() < full.requests.size() / 10);
        int states = 0, sentStates = 0, other = 0;
        for (const auto& b : after.data)
            if (isStateParam (b.param))
                ++states;
        for (const auto& q : diff.requests)
            (isStateParam (q.param) ? sentStates : other)++;
        CHECK (6, sentStates >= states && other > 0);
        const auto none = writePlan (before, &before);   // nothing changed: only the states
        bool onlyStates = ! none.requests.empty();
        for (const auto& q : none.requests)
            onlyStates = onlyStates && isStateParam (q.param);
        CHECK (6, onlyStates);
        auto mismatch = after;   // another layout than `before`: the whole plan
        mismatch.data.pop_back();
        auto mismatchCopy = mismatch;
        CHECK (6, writePlan (mismatch, &before).requests.size() == writePlan (mismatchCopy).requests.size());
    }

    // 6e. Read-back: the note values of a slot that is empty on both sides are ignored, those of a used slot are not
    {
        auto w = blank();
        Pattern a;
        a.length = 4;
        a.steps[0].on = true;
        a.steps[0].notes = { { 64, 64, 100, 0, 0 } };
        CHECK (6, writePattern (w, 2, 1, a).empty());
        auto back = w;
        for (auto prm : pp::noteParams)
            *cell (back, item::track2, prm, slotPath (1, 5)) = 7;   // slot 6 is empty: the device keeps its defaults
        CHECK (6, compareReadBack (w, back).empty());
        *cell (back, item::track2, pp::noteParams[0], slotPath (1, 0)) = 70;   // slot 1 holds the note
        CHECK (6, compareReadBack (w, back).size() == 1);
    }

    // 7. The edited project goes through the restore plan: states last, the new notes are in the requests
    auto q = blank();
    CHECK (7, writePattern (q, 4, 2, pt).empty());
    const auto w = writePlan (q);
    bool noteSent = false, stateLast = true, seenState = false;
    for (const auto& r : w.requests)
    {
        seenState = seenState || isStateParam (r.param);
        stateLast = stateLast && (! seenState || isStateParam (r.param));
        noteSent = noteSent || (r.item == item::track4 && r.param == pp::noteParams[0] && r.path[0] == 2 && r.values[0] == 60);
    }
    CHECK (7, noteSent && stateLast);

    std::printf (failures == 0 ? "pattern_test: OK\n" : "pattern_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
