// backup_test: Source/Ksp/RecallPlan.h and Backup.h with a fake device. No JUCE.
#include <cstdio>
#include <map>
#include "Ksp/Backup.h"

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

int main()
{
    using namespace ksp;

    // 1. The plan: 85 groups, 8951 requests per project, starting with the project block (item 0x78, param 37)
    const auto plan = plan::expandPlan();
    CHECK (1, plan::groups().size() == 85);
    CHECK (1, plan.size() == 8951);
    CHECK (1, plan.front().item == 0x78 && plan.front().param == 37 && plan.front().path == std::vector<std::uint8_t> { 1 });
    std::size_t values = 0;
    for (const auto& r : plan)
        values += r.count;
    std::printf ("plan: %zu requests, %zu values per project\n", plan.size(), values);

    // 2. Read with a fake device: every value 60, except pattern 1 of track 4 valid (state 3), and one block cut short
    //    (track 2, pattern 1, pitches of slots 1-16, the 3rd unreadable) that is re-read value by value.
    int calls = 0;
    BulkRead fake = [&] (int project, std::uint8_t prm, std::uint8_t it, const std::vector<std::uint8_t>& path,
                         std::uint8_t count) -> std::optional<Bytes> {
        ++calls;
        if (project != 3)
            return std::nullopt;
        if (it == item::track4 && prm == param::patternState && path[0] == 1)
            return Bytes { 3 };
        if (it == item::track2 && prm == param::notePitch && path.size() == 3 && path[0] == 1 && path[1] == 1
            && path[2] <= 16)
        {
            if (count == 16)
                return Bytes { 60, 64 };            // cut at the 3rd value
            return path[2] == 3 ? Bytes {} : Bytes { 61 };
        }
        return Bytes (count, 60);
    };

    double last = -1;
    const auto r = readProject (3, fake, [&] (double p) { last = p; return true; }, "2026-10-29T00:00:00Z");
    CHECK (2, r.project.has_value() && r.error.empty());
    CHECK (2, last == 1.0);
    if (r.project)
    {
        const auto& p = *r.project;
        CHECK (2, p.data.size() == plan.size());
        CHECK (2, blockValue (p, item::track4, param::patternState, 1) == 3);
        CHECK (2, summary (p) == "T1 0, T2 0, T3 0, T4 1 patterns, 0 scenes");
        bool chordChecked = false;
        for (const auto& b : p.data)
            if (b.item == item::track2 && b.param == param::notePitch && b.path == std::vector<std::uint8_t> { 1, 1, 1 })
            {
                CHECK (2, b.values.size() == 16 && b.values[0] == 61 && b.values[2] == kUnreadable);
                chordChecked = true;
            }
        CHECK (2, chordChecked);

        // 3. JSON: KSCON's format, unreadable values as null
        BackupData backup;
        backup.saved = "2026-10-29T00:00:00Z";
        backup.globals = { { 70, 0 }, { 152, kUnreadable } };
        backup.projects.push_back (p);
        const auto js = toJson (backup);
        CHECK (3, js.rfind ("{\"format\":\"keystep-pro-backup\",\"version\":1,\"firmware\":\"2.5.20\"", 0) == 0);
        CHECK (3, js.find ("\"globals\":{\"70\":0,\"152\":null}") != std::string::npos);
        CHECK (3, js.find ("\"format\":\"keystep-pro-project\",\"version\":1,\"firmware\":\"2.5.20\",\"project\":3") != std::string::npos);
        CHECK (3, js.find ("[120,37,[1],[60]]") != std::string::npos);
        CHECK (3, js.find ("[61,61,null,61,") != std::string::npos);
    }

    // 4. No reply: an error naming the request, nothing returned
    const auto bad = readProject (5, fake, nullptr, "x");
    CHECK (4, ! bad.project && bad.error.find ("project 5") != std::string::npos);

    // 5. Stop from the progress callback
    const auto stopped = readProject (3, fake, [] (double) { return false; }, "x");
    CHECK (5, ! stopped.project && stopped.error == "stopped");

    // 6. A cut-short reply parses with fewer values; a full one with all
    const Bytes cut { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 0x01, 87, 0x01, 0x7C, 0x01, 16, 60, 64, 0xF7 };
    const auto v = parseBulkValues (cut, 1, 87, item::track2, { 1 }, 16);
    CHECK (6, v && v->size() == 2);
    CHECK (6, ! parseBulk (cut, 1, 87, item::track2, { 1 }, 16));

    // 9. Reading one pattern again: about 200 requests of the track, only its blocks change in the project
    if (r.project)
    {
        const auto which = patternRequests (item::track1, 3);
        CHECK (9, which.size() > 150 && which.size() < 300);
        CHECK (9, patternRequests (item::track1, 3) == which && patternRequests (item::track2, 3) != which);
        bool chordLeft = false;
        for (auto n : which)
            chordLeft = chordLeft || plan[n].param == 0x57;
        CHECK (9, ! chordLeft);

        auto project = *r.project;
        BulkRead changed = [&] (int pr, std::uint8_t prm, std::uint8_t it, const std::vector<std::uint8_t>& path, std::uint8_t count) -> std::optional<Bytes> {
            if (it == item::track1 && prm == param::notePitch && path.size() == 3 && path[0] == 3 && path[1] == 1 && path[2] == 1)
                return Bytes (count, 72);   // the pitches of the first slots of pattern 3, as if a note was added on the KeyStep
            return fake (pr, prm, it, path, count);
        };
        std::string error;
        calls = 0;
        const auto updates = readBlocks (3, which, changed, nullptr, error);
        CHECK (9, updates.has_value() && updates->size() == which.size() && calls >= static_cast<int> (which.size()) - 1);   // the simulated read does not go through the counter
        const int n = updates ? applyUpdates (project, *updates) : -1;
        CHECK (9, n >= 1 && n < 40);   // the changed pitches, nothing outside the pattern
        bool others = true;
        for (std::size_t i = 0; i < project.data.size(); ++i)
            if (std::find (which.begin(), which.end(), i) == which.end())
                others = others && project.data[i].values == r.project->data[i].values;
        CHECK (9, others);
        CHECK (9, applyUpdates (project, *updates) == 0);   // reading the same again changes nothing
        CHECK (9, ! readBlocks (5, which, fake, nullptr, error).has_value() && ! error.empty());   // no reply from the device
    }

    // 7. Globals table
    CHECK (7, std::size (kGlobalSettings) == 129 && kGlobalSettings[0].id == 70);

    std::printf (failures == 0 ? "backup_test: OK\n" : "backup_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
