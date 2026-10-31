// gen_test: Source/Gen (the ambient generator, the role engine and the style presets). No JUCE.
#include <cstdio>
#include "Gen/Styles.h"

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

static ksp::ProjectData blank()
{
    using namespace ksp;
    ProjectData p;
    p.project = 16;
    for (const auto& r : plan::expandPlan())
        p.data.push_back ({ r.item, r.param, r.path, std::vector<int> (r.count, 0) });
    for (int t = 1; t <= 4; ++t)
        for (int pat = 1; pat <= 16; ++pat)
            for (int s = 0; s < model::kSlots; ++s)
                *cell (p, item::track (t), model::pp::step, model::slotPath (pat, s)) = kEmpty;
    return p;
}

int main()
{
    using namespace ksp;
    using namespace ksp::model;

    gen::Params params;
    params.seed = 7;

    // 1. Every generated pattern is writable, has the track's length and only scale notes in its range
    const auto& iv = gen::intervals (params.scale);
    for (int t = 1; t <= 4; ++t)
        for (int v = 0; v < 4; ++v)
        {
            const auto pt = gen::makePattern (params, t, v, Note {});
            CHECK (1, check (pt).empty());
            CHECK (1, pt.length == gen::kLengths[t - 1] && pt.valid);
            int notes = 0;
            for (const auto& s : pt.steps)
                for (const auto& n : s.notes)
                {
                    ++notes;
                    const int rel = ((n.pitch - params.root) % 12 + 12) % 12;
                    CHECK (1, std::find (iv.begin(), iv.end(), rel) != iv.end());
                    CHECK (1, n.pitch >= 24 && n.pitch <= 108);
                }
            CHECK (1, notes > 0);
            for (int s = pt.length; s < kSteps; ++s)
                CHECK (1, pt.steps[static_cast<std::size_t> (s)].notes.empty());
        }

    // 2. Same seed, same notes; another seed, other notes
    const auto a = gen::makePattern (params, 3, 0, Note {}), b = gen::makePattern (params, 3, 0, Note {});
    CHECK (2, gen::preview (a) == gen::preview (b));
    auto other = params;
    other.seed = 8;
    CHECK (2, gen::preview (gen::makePattern (other, 3, 0, Note {})) != gen::preview (a));

    // 3. Applied to a project: patterns 1-4 of tracks 1-4 become valid and read back as generated, pattern 5 is untouched
    auto p = blank();
    CHECK (3, gen::apply (p, params).empty());
    for (int t = 1; t <= 4; ++t)
    {
        for (int v = 0; v < 4; ++v)
            CHECK (3, gen::preview (readPattern (p, t, v + 1)) == gen::preview (gen::makePattern (params, t, v, Note {})));
        CHECK (3, ! readPattern (p, t, 5).valid);
    }
    CHECK (3, gen::apply (p, params, 2, 3).empty());
    CHECK (3, *cell (p, item::project, 37, { 1 }) == 3);   // the project state, as the device stores it after a commit

    // 3b. Time division per track (slow): 1/4 for drone, pad and bells, 1/8 for the melody, Poly pad; "slow = false" keeps it
    for (int t = 1; t <= 4; ++t)
        CHECK (3, divisionBits (readPattern (p, t, 1)) == (t == 3 ? 1 : 0));
    CHECK (3, isPoly (readPattern (p, 2, 1)));
    auto fast = blank();
    auto keep = params;
    keep.slow = false;
    auto untouched = blank();   // a named project: readPattern takes a non-const reference (clang refuses a temporary)
    CHECK (3, gen::apply (fast, keep).empty() && readPattern (fast, 1, 1).config == readPattern (untouched, 1, 1).config);

    // 3c. Moods: extended chords, key and scale of the mood, every note in the mood's scale; the pad plays the whole chord
    for (const char* name : { "ethereal", "dark", "meditative" })
    {
        auto mp = gen::moodParams (name, 3);
        CHECK (3, ! mp.progression.empty());
        const auto& miv = gen::intervals (mp.scale);
        auto q = blank();
        CHECK (3, gen::apply (q, mp).empty() && gen::applyScenes (q, mp).empty());
        for (int t = 1; t <= 4; ++t)
            for (int v = 0; v < 4; ++v)
            {
                const auto pt = readPattern (q, t, v + 1);
                CHECK (3, pt.valid && pt.root == mp.root && pt.scale == mp.scale);
                for (const auto& s : pt.steps)
                    for (const auto& n : s.notes)
                    {
                        const int rel = ((n.pitch - mp.root) % 12 + 12) % 12;
                        CHECK (3, std::find (miv.begin(), miv.end(), rel) != miv.end());
                        CHECK (3, n.pitch >= 24 && n.pitch <= 108);
                    }
            }
        for (int v = 0; v < 4; ++v)
            CHECK (3, readPattern (q, 2, v + 1).steps[0].notes.size() == mp.progression[static_cast<std::size_t> (v)].offsets.size());
        CHECK (3, gen::preview (readPattern (q, 3, 1)) == gen::preview (gen::makePattern (mp, 3, 0, Note {})));
    }
    CHECK (3, gen::moodParams ("nope").progression.empty() && gen::moodParams ("classic").progression.empty());

    // 4. The project goes through the restore plan
    const auto w = writePlan (p);
    CHECK (4, ! w.requests.empty());

    // 5. Regression: the ambient output (all moods, two seeds, every track and variation) is frozen by a hash of the previews
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const char* m : { "classic", "ethereal", "dark", "meditative" })
            for (unsigned seed : { 1u, 7u })
            {
                const auto gp = gen::moodParams (m, seed);
                for (int t = 1; t <= 4; ++t)
                    for (int v = 0; v < 4; ++v)
                        for (unsigned char c : gen::preview (gen::makePattern (gp, t, v, Note {})))
                            h = (h ^ c) * 1099511628211ull;
            }
        std::printf ("ambient hash %llu\n", static_cast<unsigned long long> (h));
        CHECK (5, h == 17786799780408337041ull);
    }

    // 6. Roles: euclid, bass, drums
    {
        const auto e = gen::euclid (4, 16);
        int n = 0;
        for (std::size_t i = 0; i < e.size(); ++i)
            n += e[i] ? 1 : 0;
        CHECK (6, e.size() == 16 && n == 4 && e[0] && e[4] && e[8] && e[12]);
        CHECK (6, gen::euclid (0, 8) == std::vector<bool> (8, false) && gen::euclid (20, 4) == std::vector<bool> (4, true));
        CHECK (6, gen::euclid (3, 8, 1)[1] && gen::euclid (3, 8, -1)[7] && gen::euclid (3, 0).empty());

        auto mp = gen::moodParams ("dark", 5);
        gen::RoleSpec bass { gen::Role::bass, 16, 100, -1, 77 };
        const auto b1 = gen::makeRolePattern (mp, bass, 0, Note {}), b2 = gen::makeRolePattern (mp, bass, 0, Note {});
        CHECK (6, gen::preview (b1) == gen::preview (b2) && check (b1).empty());
        CHECK (6, ! b1.steps[0].notes.empty() && b1.steps[0].notes[0].pitch >= 36 && b1.steps[0].notes[0].pitch < 60);
        int bassNotes = 0;
        for (const auto& s : b1.steps)
            bassNotes += static_cast<int> (s.notes.size());
        CHECK (6, bassNotes >= 2 && bassNotes <= 16);
        gen::RoleSpec high = bass;
        high.low = 48;
        CHECK (6, gen::makeRolePattern (mp, high, 0, Note {}).steps[0].notes[0].pitch >= 48);

        gen::DrumSpec ds;
        ds.hatEvery = 2;
        ds.openHatChance = 30;
        ds.ghostChance = 15;
        ds.swingShift = 10;
        const auto d = gen::makeDrumPattern (mp, ds, 0);
        CHECK (6, d.valid && d.length == 16 && check (d).empty());
        int kicks = 0, snares = 0, hats = 0;
        for (const auto& h : d.hits)
        {
            kicks += h.lane == 0;
            snares += h.lane == 2;
            hats += h.lane == 6 || h.lane == 10;
        }
        CHECK (6, kicks == 4 && snares >= 2 && hats == 8);
        const auto d2 = gen::makeDrumPattern (mp, ds, 0);
        CHECK (6, d.hits.size() == d2.hits.size() && d.hits[3].velocity == d2.hits[3].velocity);
        // the groove goes into a project
        auto proj = p;
        CHECK (6, writeDrumPattern (proj, 1, d).empty() && readDrumPattern (proj, 1).hits.size() == d.hits.size());
        gen::DrumSpec odd;
        odd.length = 3;
        odd.kickPulses = 9;
        CHECK (6, check (gen::makeDrumPattern (mp, odd, 1)).empty());
    }

    // 7. Styles: drums on track 1 (drum mode), roles on 2-4, valid and deterministic; fixed kicks, offbeat hats, fill
    {
        CHECK (7, gen::styles().size() == 3 && gen::findStyle ("house") != nullptr && gen::findStyle ("dark") == nullptr);
        for (const auto& st : gen::styles())
        {
            auto proj = p;
            auto sp = gen::styleParams (st, 3);
            CHECK (7, gen::applyStyle (proj, st, sp).empty() && gen::applyScenes (proj, sp, false).empty());
            CHECK (7, isDrumMode (proj));
            for (int v = 1; v <= 4; ++v)
            {
                const auto d = readDrumPattern (proj, v);
                CHECK (7, d.valid && check (d).empty() && d.hits.size() >= 8);
                for (int t = 2; t <= 4; ++t)
                {
                    const auto pt = readPattern (proj, t, v);
                    CHECK (7, pt.valid && check (pt).empty());
                    CHECK (7, divisionBits (pt) == static_cast<int> (st.tracks[t - 2].division) && isPoly (pt) == st.tracks[t - 2].poly);
                    int notes = 0;
                    for (const auto& s : pt.steps)
                        for (const auto& n : s.notes)
                        {
                            ++notes;
                            const auto& siv = gen::intervals (st.scale);
                            CHECK (7, std::find (siv.begin(), siv.end(), ((n.pitch - st.root) % 12 + 12) % 12) != siv.end());
                        }
                    CHECK (7, notes > 0);
                }
            }
            CHECK (7, readScene (proj, 4).valid);
            auto again = p;
            CHECK (7, gen::applyStyle (again, st, sp).empty() && readDrumPattern (again, 2).hits.size() == readDrumPattern (proj, 2).hits.size());
        }
        const auto& dnb = *gen::findStyle ("dnb");
        const auto groove = gen::makeDrumPattern (gen::styleParams (dnb), dnb.drums, 0);
        std::vector<int> kickAt;
        for (const auto& h : groove.hits)
            if (h.lane == 0)
                kickAt.push_back (h.step);
        CHECK (7, kickAt == std::vector<int> ({ 0, 10 }));
        const auto& house = *gen::findStyle ("house");
        int open = 0;
        for (const auto& h : gen::makeDrumPattern (gen::styleParams (house), house.drums, 0).hits)
            open += h.lane == 10 && h.step % 4 == 2;
        CHECK (7, open == 4);
        auto withFill = dnb.drums;
        withFill.fill = true;
        CHECK (7, gen::makeDrumPattern (gen::styleParams (dnb), withFill, 0).hits.size() > groove.hits.size());
        gen::RoleSpec stab { gen::Role::stab, 16, -1, 60, 5, 4, 2, 20 };
        const auto sp = gen::makeRolePattern (gen::styleParams (house), stab, 0, Note {});
        CHECK (7, sp.steps[2].notes.size() == 5 && sp.steps[6].notes.size() == 5 && sp.steps[0].notes.empty() && sp.steps[2].notes[0].gate == 20);
    }

    std::printf ("track 3 pattern 1: %s\n", gen::preview (a).c_str());
    std::printf (failures == 0 ? "gen_test: OK\n" : "gen_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
