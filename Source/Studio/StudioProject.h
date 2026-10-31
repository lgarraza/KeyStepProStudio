/*
    StudioProject.h
    One project of the Studio library: everything a piece of ambient music needs, kept by the app (the KeyStep Pro's own memory is
    only a volatile working copy of it, see CLAUDE.md):
      - the KeyStep Pro project (patterns, scenes, settings; the recall plan's values, Backup.h) as `ksp`; its project number is a
        placeholder, the slot is chosen when the project is loaded into the KeyStep;
      - the four tracks' plugins: one instrument and a chain of effects per track, each as the plugin's description and its state
        (opaque text here: the JUCE side fills and reads them), plus volume / mute / solo;
      - how it was generated (mood, seed...) and the user's name, notes and dates.
    A project in this file format round-trips exactly. C++17 + std, no JUCE.

    File ("keystepro-studio-project", version 1): one JSON object, the KeyStep Pro project embedded in the format of Backup.h.
*/

#pragma once

#include <array>
#include <cstdio>
#include <ctime>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include "../Ksp/Restore.h"

namespace ksp::studio
{

struct PluginSetup
{
    std::string name;          // for lists and for a plugin that is missing on this machine
    std::string description;   // the plugin's description (JUCE's XML), kept as it is
    std::string state;         // the plugin's state, base64 text, kept as it is
    bool bypass = false;

    bool operator== (const PluginSetup& o) const
    {
        return name == o.name && description == o.description && state == o.state && bypass == o.bypass;
    }
};

struct TrackSetup
{
    std::optional<PluginSetup> instrument;
    std::vector<PluginSetup> effects;   // in series, after the instrument
    double volume = 0.8;                // 0..1
    bool mute = false, solo = false;

    bool operator== (const TrackSetup& o) const
    {
        return instrument == o.instrument && effects == o.effects && volume == o.volume && mute == o.mute && solo == o.solo;
    }
};

struct Generator
{
    std::string mood = "classic";   // classic, ethereal, dark, meditative (Gen/Ambient.h)
    unsigned seed = 1;
    bool chains = false, slow = true;

    bool operator== (const Generator& o) const { return mood == o.mood && seed == o.seed && chains == o.chains && slow == o.slow; }
};

struct StudioProject
{
    std::string id;        // 32 hex characters, set when it is first saved in a library
    std::string name;
    std::string created, modified;   // ISO 8601, UTC
    std::string notes;
    Generator generator;
    std::array<TrackSetup, 4> tracks;
    std::vector<PluginSetup> masterFx;   // VST3 effects in series on the whole mix, before the limiter
    ProjectData ksp;       // ksp.project is a placeholder (1)
};

// ---------------------------------------------------------------- small helpers

inline std::string nowIso()
{
    const std::time_t t = std::time (nullptr);
    std::tm tm {};
#ifdef _WIN32
    gmtime_s (&tm, &t);
#else
    gmtime_r (&t, &tm);
#endif
    char buf[32];
    std::strftime (buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline std::string newId()
{
    std::random_device rd;
    std::mt19937_64 e ((static_cast<std::uint64_t> (rd()) << 32) ^ rd() ^ static_cast<std::uint64_t> (std::time (nullptr)));
    static const char* hex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i)
        id += hex[e() & 15];
    return id;
}

/** A JSON string: quotes, backslashes and control characters escaped; UTF-8 bytes pass through untouched. */
inline std::string esc (const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf (buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                }
                else
                    o += static_cast<char> (c);
        }
    }
    return o + "\"";
}

inline std::string pluginJson (const PluginSetup& p)
{
    return "{\"name\":" + esc (p.name) + ",\"description\":" + esc (p.description) + ",\"state\":" + esc (p.state)
           + ",\"bypass\":" + (p.bypass ? "true" : "false") + "}";
}

inline std::string numberText (double v)
{
    char buf[32];
    std::snprintf (buf, sizeof buf, "%.6g", v);
    return buf;
}

// ---------------------------------------------------------------- JSON

inline std::string toJson (const StudioProject& p, const std::string& firmware = "2.5.20")
{
    std::string o = "{\"format\":\"keystepro-studio-project\",\"version\":1,\"id\":" + esc (p.id) + ",\"name\":" + esc (p.name)
                    + ",\"created\":" + esc (p.created) + ",\"modified\":" + esc (p.modified) + ",\"notes\":" + esc (p.notes)
                    + ",\"generator\":{\"mood\":" + esc (p.generator.mood) + ",\"seed\":" + std::to_string (p.generator.seed)
                    + ",\"chains\":" + (p.generator.chains ? "true" : "false") + ",\"slow\":" + (p.generator.slow ? "true" : "false")
                    + "},\"tracks\":[";
    for (std::size_t t = 0; t < p.tracks.size(); ++t)
    {
        const auto& tr = p.tracks[t];
        o += (t ? "," : "") + std::string ("{\"instrument\":") + (tr.instrument ? pluginJson (*tr.instrument) : "null") + ",\"effects\":[";
        for (std::size_t i = 0; i < tr.effects.size(); ++i)
            o += (i ? "," : "") + pluginJson (tr.effects[i]);
        o += "],\"volume\":" + numberText (tr.volume) + ",\"mute\":" + (tr.mute ? "true" : "false") + ",\"solo\":"
             + (tr.solo ? "true" : "false") + "}";
    }
    o += "],\"fx\":{\"master\":[";
    for (std::size_t i = 0; i < p.masterFx.size(); ++i)
        o += (i ? "," : "") + pluginJson (p.masterFx[i]);
    o += "]}";
    return o + ",\"ksp\":" + ksp::toJson (p.ksp, firmware) + "}\n";
}

struct LoadResult
{
    std::optional<StudioProject> project;
    std::string error;
};

namespace detail
{
    inline std::string text (const json::Value& o, const char* key)
    {
        const auto* v = o.get (key);
        return v != nullptr && v->type == json::Value::string ? v->str : std::string();
    }

    inline bool flag (const json::Value& o, const char* key, bool fallback)
    {
        const auto* v = o.get (key);
        return v != nullptr && v->type == json::Value::boolean ? v->num != 0 : fallback;
    }

    inline std::optional<PluginSetup> pluginFrom (const json::Value& v)
    {
        if (v.type != json::Value::object)
            return std::nullopt;
        PluginSetup p;
        p.name = text (v, "name");
        p.description = text (v, "description");
        p.state = text (v, "state");
        p.bypass = flag (v, "bypass", false);
        return p;
    }
}

inline LoadResult fromJson (const std::string& textIn)
{
    const auto root = json::Reader (textIn).parse();
    if (! root || root->type != json::Value::object)
        return { std::nullopt, "not a JSON file" };
    const auto* fmt = root->get ("format");
    const auto* version = root->get ("version");
    if (fmt == nullptr || fmt->str != "keystepro-studio-project" || version == nullptr || ! version->isInt()
        || static_cast<int> (version->num) != 1)
        return { std::nullopt, "not a KeyStepPro Studio project" };

    StudioProject p;
    p.id = detail::text (*root, "id");
    p.name = detail::text (*root, "name");
    p.created = detail::text (*root, "created");
    p.modified = detail::text (*root, "modified");
    p.notes = detail::text (*root, "notes");
    if (const auto* g = root->get ("generator"); g != nullptr && g->type == json::Value::object)
    {
        p.generator.mood = detail::text (*g, "mood");
        if (const auto* s = g->get ("seed"); s != nullptr && s->type == json::Value::number)
            p.generator.seed = static_cast<unsigned> (s->num);
        p.generator.chains = detail::flag (*g, "chains", false);
        p.generator.slow = detail::flag (*g, "slow", true);
    }
    if (const auto* tracks = root->get ("tracks"); tracks != nullptr && tracks->type == json::Value::array)
    {
        if (tracks->items.size() != p.tracks.size())
            return { std::nullopt, "a Studio project has 4 tracks" };
        for (std::size_t t = 0; t < p.tracks.size(); ++t)
        {
            const auto& tv = tracks->items[t];
            if (tv.type != json::Value::object)
                return { std::nullopt, "track " + std::to_string (t + 1) + " is not an object" };
            if (const auto* inst = tv.get ("instrument"))
                p.tracks[t].instrument = detail::pluginFrom (*inst);
            if (const auto* fx = tv.get ("effects"); fx != nullptr && fx->type == json::Value::array)
                for (const auto& e : fx->items)
                    if (auto pl = detail::pluginFrom (e))
                        p.tracks[t].effects.push_back (std::move (*pl));
            if (const auto* vol = tv.get ("volume"); vol != nullptr && vol->type == json::Value::number)
                p.tracks[t].volume = vol->num;
            p.tracks[t].mute = detail::flag (tv, "mute", false);
            p.tracks[t].solo = detail::flag (tv, "solo", false);
        }
    }
    if (const auto* f = root->get ("fx"); f != nullptr && f->type == json::Value::object)
    {
        if (const auto* master = f->get ("master"); master != nullptr && master->type == json::Value::array)
            for (const auto& e : master->items)
                if (auto pl = detail::pluginFrom (e))
                    p.masterFx.push_back (std::move (*pl));
    }
    const auto* k = root->get ("ksp");
    if (k == nullptr)
        return { std::nullopt, "the project has no KeyStep Pro data" };
    std::string error;
    auto ksp = ksp::detail::projectFrom (*k, error);
    if (! ksp)
        return { std::nullopt, error };
    p.ksp = std::move (*ksp);
    return { std::move (p), {} };
}

/** A new project around a KeyStep Pro project read from the device or a file. */
inline StudioProject fromKsp (ProjectData data, const std::string& name)
{
    StudioProject p;
    p.name = name;
    p.ksp = std::move (data);
    p.ksp.project = 1;
    return p;
}

} // namespace ksp::studio
