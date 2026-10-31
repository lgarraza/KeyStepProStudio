// restore_test: Source/Ksp/Restore.h (JSON reader, send order, drum fix, read-back) with a fake device. No JUCE.
#include <cstdio>
#include <map>
#include "Ksp/Restore.h"

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

int main()
{
    using namespace ksp;

    // A fake device: a map of single values, read and written through the same callbacks as the tools use.
    std::map<std::string, int> mem;
    auto key = [] (std::uint8_t it, std::uint8_t prm, const std::vector<std::uint8_t>& path) {
        std::string k = std::to_string (it) + "/" + std::to_string (prm);
        for (auto x : path) k += "/" + std::to_string (x);
        return k;
    };
    auto at = [&] (std::uint8_t it, std::uint8_t prm, std::vector<std::uint8_t> path, std::size_t j) {
        path.back() = static_cast<std::uint8_t> (path.back() + j);
        return key (it, prm, path);
    };
    BulkRead read = [&] (int, std::uint8_t prm, std::uint8_t it, const std::vector<std::uint8_t>& path,
                         std::uint8_t count) -> std::optional<Bytes> {
        Bytes out;
        for (std::size_t j = 0; j < count; ++j)
        {
            const auto f = mem.find (at (it, prm, path, j));
            out.push_back (static_cast<std::uint8_t> (f == mem.end() ? 0 : f->second));
        }
        return out;
    };
    auto write = [&] (const WriteRequest& r) {
        for (std::size_t j = 0; j < r.values.size(); ++j)
            mem[at (r.item, r.param, r.path, j)] = r.values[j];
    };

    // A project to restore: every value 5, track 4 pattern 1 valid, track 1 pattern 2 valid with an empty drum slot
    // that carries lane 60, track 2's chord with an unreadable byte, one unreadable note pitch.
    std::map<std::string, int> source;
    BulkRead src = [&] (int, std::uint8_t prm, std::uint8_t it, const std::vector<std::uint8_t>& path,
                        std::uint8_t count) -> std::optional<Bytes> {
        Bytes out;
        for (std::size_t j = 0; j < count; ++j)
        {
            const auto f = source.find (at (it, prm, path, j));
            out.push_back (static_cast<std::uint8_t> (f == source.end() ? 5 : f->second));
        }
        return out;
    };
    source[key (item::track4, param::patternState, { 1 })] = 3;
    source[key (item::track1, param::patternState, { 2 })] = 3;
    source[key (item::track1, 0x36, { 2, 1, 7 })] = 127;    // empty drum slot
    source[key (item::track1, 0x75, { 2, 1, 7 })] = 60;     // lane left over
    source[key (item::track1, 0x36, { 3, 1, 7 })] = 127;    // same in a default pattern: untouched
    source[key (item::track1, 0x75, { 3, 1, 7 })] = 60;
    source[key (item::track3, param::trackState, { 1 })] = 2;
    auto r = readProject (16, src, nullptr, "2026-10-29T00:00:00Z");
    CHECK (1, r.project.has_value());
    if (! r.project)
        return 1;
    ProjectData p = *r.project;
    p.project = 1;
    *cell (p, item::track2, kChordParam, { 4 }) = kUnreadable;
    *cell (p, item::track2, param::notePitch, { 1, 1, 3 }) = kUnreadable;

    // 1. JSON round trip: the backup file reads back block for block, nulls included; a single project file too
    BackupData b;
    b.saved = "2026-10-29T00:00:00Z";
    b.globals = { { 70, 1 }, { 152, kUnreadable } };
    b.projects.push_back (p);
    const auto loaded = fromJson (toJson (b));
    CHECK (1, loaded.backup.has_value() && loaded.error.empty());
    if (loaded.backup)
    {
        CHECK (1, loaded.backup->projects.size() == 1 && loaded.backup->projects[0].project == 1);
        CHECK (1, loaded.backup->globals.size() == 2 && loaded.backup->globals[1].value == kUnreadable);
        const auto& q = loaded.backup->projects[0];
        bool same = q.data.size() == p.data.size();
        for (std::size_t n = 0; same && n < p.data.size(); ++n)
            same = q.data[n].item == p.data[n].item && q.data[n].param == p.data[n].param
                   && q.data[n].path == p.data[n].path && q.data[n].values == p.data[n].values;
        CHECK (1, same);
    }
    const auto single = fromJson (toJson (p, "2.5.20"));
    CHECK (1, single.backup && single.backup->projects.size() == 1 && single.backup->globals.empty());

    // 2. Bad files are refused with a reason
    CHECK (2, ! fromJson ("{\"format\":\"other\"}").backup);
    CHECK (2, ! fromJson ("not json").backup);
    auto shortP = p;
    shortP.data.pop_back();
    CHECK (2, checkProject (shortP).find ("blocks") != std::string::npos);
    auto twice = b;
    twice.projects.push_back (p);
    CHECK (2, fromJson (toJson (twice)).error.find ("twice") != std::string::npos);

    // 3. Send order: states last, the bad chord left out, the unreadable pitch skipped, the drum slot fixed
    auto written = p;
    const auto w = writePlan (written);
    CHECK (3, w.drumFixed == 5);   // lane, gate, velocity, shift, random
    CHECK (3, *cell (written, item::track1, 0x75, { 2, 1, 7 }) == 127 && *cell (written, item::track1, 0x75, { 3, 1, 7 }) == 60);
    CHECK (3, w.notes.size() == 2 && w.notes[0].find ("track 2 chord") == 0);
    std::size_t firstState = w.requests.size();
    bool statesLast = true, chordSent = false;
    for (std::size_t n = 0; n < w.requests.size(); ++n)
    {
        const auto& q = w.requests[n];
        if (isStateParam (q.param))
            firstState = std::min (firstState, n);
        else if (n > firstState)
            statesLast = false;
        chordSent = chordSent || (q.item == item::track2 && q.param == kChordParam);
    }
    CHECK (3, statesLast && ! chordSent && firstState < w.requests.size());
    std::size_t pitchReqs = 0;
    for (const auto& q : w.requests)
        if (q.item == item::track2 && q.param == param::notePitch && q.path.size() == 3 && q.path[0] == 1 && q.path[1] == 1 && q.path[2] <= 16)
            ++pitchReqs;
    CHECK (3, pitchReqs == 15);   // the cut block goes one value at a time, without the unreadable one

    // 4. Read-back on the fake device: as written; a track written as default reads valid and is accepted
    for (const auto& q : w.requests)
        write (q);
    mem[key (item::track3, param::trackState, { 1 })] = 3;
    mem[key (item::project, 56, { 5 })] = 0;   // not stored by the device
    auto back = readProject (1, read, nullptr, "x");
    CHECK (4, back.project.has_value());
    if (back.project)
    {
        const auto problems = compareReadBack (written, *back.project);
        CHECK (4, problems.empty());
        for (const auto& s : problems)
            std::printf ("  %s\n", s.c_str());
        mem[key (item::track4, param::notePitch, { 2, 1, 1 })] = 99;
        const auto again = readProject (1, read, nullptr, "x");
        const auto one = compareReadBack (written, *again.project);
        CHECK (4, one.size() == 1 && one[0].find ("item 126 param 109 [2,1,1] reads 99") == 0);
    }

    // 5. Frames: bulk SET layout, commit, and the write guard (one slot only; reads are not writes)
    const auto f = bulkSet (16, param::notePitch, item::track1, { 1, 1, 1 }, { 60, 200 });
    CHECK (5, (f == Bytes { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 16, 109, 3, 0x7B, 1, 1, 1, 2, 60, 200 & 0x7F, 0xF7 }));
    CHECK (5, (commit (16) == Bytes { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x06, 16, 0xF7 }));
    CHECK (5, isWriteFor (f, 16) && isWriteFor (commit (16), 16) && isWriteFor (openSession (16), 16));
    CHECK (5, ! isWriteFor (f, 1) && ! isWriteFor (commit (1), 16) && ! isWriteFor (bulkGet (16, 109, 0x7B, { 1 }, 1), 16));
    CHECK (5, ! isReadOnlyRequest (f) && ! isReadOnlyRequest (commit (16)));

    std::printf (failures == 0 ? "restore_test: OK\n" : "restore_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
