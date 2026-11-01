// studio_test: Source/Studio/StudioProject.h and Library.h (the Studio project file and the library folder). No JUCE.
#include <cstdio>
#include "Studio/Library.h"

static int failures = 0;

#define CHECK(n, ...)                                                \
    do {                                                             \
        if (! (__VA_ARGS__)) { std::printf ("FAIL %d: %s\n", n, #__VA_ARGS__); ++failures; } \
    } while (0)

static ksp::ProjectData blank()
{
    using namespace ksp;
    ProjectData p;
    p.project = 1;
    for (const auto& r : plan::expandPlan())
        p.data.push_back ({ r.item, r.param, r.path, std::vector<int> (r.count, 0) });
    return p;
}

static bool sameKsp (const ksp::ProjectData& a, const ksp::ProjectData& b)
{
    if (a.data.size() != b.data.size())
        return false;
    for (std::size_t i = 0; i < a.data.size(); ++i)
        if (a.data[i].item != b.data[i].item || a.data[i].param != b.data[i].param || a.data[i].path != b.data[i].path
            || a.data[i].values != b.data[i].values)
            return false;
    return true;
}

int main()
{
    using namespace ksp;
    using namespace ksp::studio;

    // 1. A project round-trips exactly: accents (UTF-8), quotes, newlines and backslashes in names, notes and plugin XML, a missing
    //    instrument, an effect chain, unreadable values, the generator settings
    StudioProject p = fromKsp (blank(), "Lluvia en el jard\xC3\xADn \"v2\"");
    p.notes = "l\xC3\xADnea 1\nl\xC3\xADnea 2\twith \\ backslash";
    p.generator = { "ethereal", 7, true, false, "house", true, 5, 4 };
    p.tracks[0].instrument = PluginSetup { "Analog Lab V", "<PLUGIN name=\"Analog Lab V\" uid=\"1\">\n  <x a='b'/>\n</PLUGIN>", "QUJDREVGRw==", false };
    p.tracks[0].effects = { PluginSetup { "ValhallaDelay", "<PLUGIN name=\"VD\"/>", "c3RhdGU=", true }, PluginSetup { "Reverb", "<r/>", "", false } };
    p.tracks[0].volume = 0.65;
    p.tracks[2].mute = true;
    p.tracks[3].solo = true;
    p.ksp.data[5].values[0] = kUnreadable;
    p.ksp.data[7].values[0] = 99;
    p.masterFx = { PluginSetup { "Bus FORCE", "<PLUGIN name=\"Bus FORCE\"/>", "Rk9SQ0U=", true } };
    p.created = "2026-10-29T10:00:00Z";
    p.modified = "2026-10-29T11:00:00Z";
    p.id = newId();
    CHECK (1, p.id.size() == 32);
    const auto back = studio::fromJson (studio::toJson (p));
    CHECK (1, back.project.has_value() && back.error.empty());
    if (back.project)
    {
        const auto& q = *back.project;
        CHECK (1, q.id == p.id && q.name == p.name && q.notes == p.notes && q.created == p.created && q.modified == p.modified);
        CHECK (1, q.generator == p.generator);
        CHECK (1, q.masterFx == p.masterFx);
        for (std::size_t t = 0; t < 4; ++t)
            CHECK (1, q.tracks[t] == p.tracks[t]);
        CHECK (1, sameKsp (q.ksp, p.ksp));
        CHECK (1, ! q.tracks[1].instrument.has_value() && q.tracks[0].effects.size() == 2 && q.tracks[0].effects[0].bypass);
    }

    // 2. Not a project: a KeyStep Pro backup, text, a wrong version, three tracks
    CHECK (2, ! studio::fromJson ("not json").project);
    CHECK (2, ! studio::fromJson (ksp::toJson (blank(), "2.5.20")).project);
    auto text = studio::toJson (p);
    auto bad = text;
    bad.replace (bad.find ("\"version\":1"), 11, "\"version\":2");
    CHECK (2, ! studio::fromJson (bad).project);

    // 3. The library: save, list, load, duplicate, rename by saving again, remove
    const auto dir = fs::temp_directory_path() / ("ksp_library_test_" + newId());
    {
        Library lib (dir / "lib");
        CHECK (3, lib.list().empty());
        StudioProject a = fromKsp (blank(), "Primero");
        a.tracks[0].instrument = PluginSetup { "Pad One", "<p/>", "AA==", false };
        CHECK (3, lib.save (a, "2026-10-29T10:00:00Z").empty() && a.id.size() == 32 && a.created == "2026-10-29T10:00:00Z");
        StudioProject b = fromKsp (blank(), "Segundo");
        CHECK (3, lib.save (b, "2026-10-29T12:00:00Z").empty());
        auto l = lib.list();
        CHECK (3, l.size() == 2 && l[0].name == "Segundo" && l[1].name == "Primero");   // newest first
        CHECK (3, l[1].instruments == std::vector<std::string> { "Pad One" } && ! l[1].summary.empty());

        std::string error;
        auto loaded = lib.load (a.id, error);
        CHECK (3, loaded.has_value() && loaded->name == "Primero" && sameKsp (loaded->ksp, a.ksp));

        const auto copyId = lib.duplicate (a.id, "Primero (copia)", error);
        CHECK (3, copyId.size() == 32 && copyId != a.id && lib.list().size() == 3);

        a.name = "Primero renombrado";
        CHECK (3, lib.save (a, "2026-10-29T13:00:00Z").empty());
        l = lib.list();
        const auto renamed = std::find_if (l.begin(), l.end(), [&] (const Entry& e) { return e.id == a.id; });
        CHECK (3, l.size() == 3 && renamed != l.end() && renamed->name == "Primero renombrado"
                      && renamed->created == "2026-10-29T10:00:00Z" && renamed->modified == "2026-10-29T13:00:00Z");

        CHECK (3, lib.remove (b.id) && lib.list().size() == 2 && ! lib.load (b.id, error).has_value());
        CHECK (3, ! lib.remove ("../etc") && ! lib.load ("../x", error).has_value());   // an id is 32 hex characters, nothing else
        StudioProject noName;
        CHECK (3, ! lib.save (noName).empty());
    }

    // 4. The index is only a cache: deleted, corrupt or out of date, it is rebuilt from the files; a bad file is skipped and reported
    {
        Library lib (dir / "lib");
        fs::remove (dir / "lib" / "index.json");
        CHECK (4, lib.list().size() == 2);
        { std::ofstream f (dir / "lib" / "index.json", std::ios::trunc); f << "{ broken"; }
        CHECK (4, lib.list().size() == 2);
        { std::ofstream f (dir / "lib" / (newId() + ".kspstudio")); f << "{ this is not a project"; }
        Library again (dir / "lib");
        CHECK (4, again.list().size() == 2 && again.problems().size() == 1);
        Library third (dir / "lib");   // the index remembers the skipped file: listing does not rebuild again
        CHECK (4, third.list().size() == 2 && third.problems().empty());
        std::size_t temps = 0;
        for (const auto& e : fs::directory_iterator (dir / "lib"))
            temps += e.path().extension() == ".tmp" ? 1 : 0;
        CHECK (4, temps == 0);   // atomic writes leave no temporary files
    }
    std::error_code ec;
    fs::remove_all (dir, ec);

    std::printf (failures == 0 ? "studio_test: OK\n" : "studio_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
