/*
    Backup.h
    Reading whole projects and the global settings of a KeyStep Pro into memory, and writing them as JSON.
    C++17 + std, no JUCE: the device is reached through a callback, so tests can use a fake one.

    File format: the one of the KSCON page ("keystep-pro-backup", version 1), so a backup made here opens there and the
    other way round. A project is the recall plan (RecallPlan.h) with the values read for each request:
        { "format": "keystep-pro-backup", "version": 1, "firmware": "2.5.20", "saved": "<ISO time>",
          "globals": { "<id>": value | null, ... },
          "projects": [ { "format": "keystep-pro-project", "version": 1, "firmware": "2.5.20", "project": 1,
                          "name": null, "saved": "<ISO time>",
                          "data": [ [item, param, [path...], [values...]], ... ] } ] }
    A value that cannot be read over SysEx is null (kUnreadable here).

    Reads show what is SAVED in flash (a read session copies the project), never the live state.
*/

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "Globals.h"
#include "Protocol.h"
#include "RecallPlan.h"

namespace ksp
{

inline constexpr int kUnreadable = -1;

struct Block
{
    std::uint8_t item;
    std::uint8_t param;
    std::vector<std::uint8_t> path;
    std::vector<int> values;   // 0..127 or kUnreadable
};

struct ProjectData
{
    int project = 0;           // 1..16
    std::string saved;         // ISO 8601
    std::vector<Block> data;   // one per recall-plan request, in plan order
};

struct GlobalValue
{
    int id;
    int value;                 // or kUnreadable
};

struct BackupData
{
    std::string saved;
    std::string firmware = "2.5.20";
    std::vector<GlobalValue> globals;   // empty = not included
    std::vector<ProjectData> projects;
};

/** One bulk GET inside the open session: the values the device sent (possibly fewer than asked, see
    parseBulkValues), or nothing when it did not answer. */
using BulkRead = std::function<std::optional<Bytes> (int project, std::uint8_t param, std::uint8_t item,
                                                     const std::vector<std::uint8_t>& path, std::uint8_t count)>;

/** Called between requests with progress 0..1; return false to stop. */
using Progress = std::function<bool (double)>;

struct ReadResult
{
    std::optional<ProjectData> project;
    std::string error;   // set when project is empty
};

/** One plan request read into a block (a reply cut short is re-read value by value); nothing when the device does not answer. */
inline std::optional<Block> readBlock (int project, const plan::Request& r, const BulkRead& read)
{
    const auto v = read (project, r.param, r.item, r.path, r.count);
    if (! v)
        return std::nullopt;

    Block b { r.item, r.param, r.path, {} };
    if (v->size() == r.count)
    {
        b.values.assign (v->begin(), v->end());
    }
    else if (r.count == 1)
    {
        b.values.push_back (kUnreadable);
    }
    else
    {   // cut short by an unmasked byte >= 0xF7: one value at a time, the unreadable ones stay null
        for (std::uint8_t j = 0; j < r.count; ++j)
        {
            auto path = r.path;
            path.back() = static_cast<std::uint8_t> (path.back() + j);
            const auto one = read (project, r.param, r.item, path, 1);
            if (! one)
                return std::nullopt;
            b.values.push_back (one->size() == 1 ? (*one)[0] : kUnreadable);
        }
    }
    return b;
}

inline std::string describe (int project, const plan::Request& r)
{
    std::string p;
    for (auto i : r.path)
        p += (p.empty() ? "" : ",") + std::to_string (i);
    return "no reply reading project " + std::to_string (project) + " (item " + std::to_string (r.item) + ", param "
           + std::to_string (r.param) + ", index " + p + ")";
}

/** Reads one project with the whole recall plan. The caller opens the read session (openSession) first. */
inline ReadResult readProject (int project, const BulkRead& read, const Progress& progress, const std::string& savedAt)
{
    const auto plan = plan::expandPlan();
    ProjectData pd;
    pd.project = project;
    pd.saved = savedAt;
    pd.data.reserve (plan.size());

    for (std::size_t n = 0; n < plan.size(); ++n)
    {
        if (progress && (n % 64) == 0 && ! progress (static_cast<double> (n) / static_cast<double> (plan.size())))
            return { std::nullopt, "stopped" };

        auto b = readBlock (project, plan[n], read);
        if (! b)
            return { std::nullopt, describe (project, plan[n]) };
        pd.data.push_back (std::move (*b));
    }
    if (progress)
        progress (1.0);
    return { std::move (pd), {} };
}

// ---------------------------------------------------------------- reading one pattern again

/** The recall-plan requests (their positions) that belong to one pattern of a track (item 0x7B..0x7E): its notes, steps, length, scale and
    playback settings, about 200 requests (a second), not the 36 s of the whole project. The track's chord is not part of a pattern. */
inline std::vector<std::size_t> patternRequests (std::uint8_t trackItem, int pattern)
{
    constexpr std::uint8_t kChord = 0x57;
    const auto plan = plan::expandPlan();
    std::vector<std::size_t> out;
    for (std::size_t n = 0; n < plan.size(); ++n)
        if (plan[n].item == trackItem && plan[n].param != kChord && ! plan[n].path.empty() && plan[n].path[0] == pattern)
            out.push_back (n);
    return out;
}

struct BlockUpdate
{
    std::size_t index;   // position in the project's blocks (the recall plan's order)
    Block block;
};

/** Reads the given requests of `project` again. Nothing (and `error` says which request) when the device does not answer. The caller opens
    the read session first. */
inline std::optional<std::vector<BlockUpdate>> readBlocks (int project, const std::vector<std::size_t>& which, const BulkRead& read,
                                                           const Progress& progress, std::string& error)
{
    const auto plan = plan::expandPlan();
    std::vector<BlockUpdate> out;
    out.reserve (which.size());
    for (std::size_t k = 0; k < which.size(); ++k)
    {
        if (progress && (k % 32) == 0 && ! progress (static_cast<double> (k) / static_cast<double> (which.size())))
        {
            error = "stopped";
            return std::nullopt;
        }
        const auto& r = plan[which[k]];
        auto b = readBlock (project, r, read);
        if (! b)
        {
            error = describe (project, r);
            return std::nullopt;
        }
        out.push_back ({ which[k], std::move (*b) });
    }
    return out;
}

/** Puts the updates into the project; returns how many blocks changed value. */
inline int applyUpdates (ProjectData& p, const std::vector<BlockUpdate>& updates)
{
    int changed = 0;
    for (const auto& u : updates)
        if (u.index < p.data.size() && p.data[u.index].values != u.block.values)
        {
            p.data[u.index].values = u.block.values;
            ++changed;
        }
    return changed;
}

// ---------------------------------------------------------------- summary

/** First value of the block with this item, param and first index, or kUnreadable. */
inline int blockValue (const ProjectData& p, std::uint8_t item, std::uint8_t param, std::uint8_t firstIndex)
{
    for (const auto& b : p.data)
        if (b.item == item && b.param == param && ! b.path.empty() && b.path[0] == firstIndex && ! b.values.empty())
            return b.values[0];
    return kUnreadable;
}

inline constexpr std::uint8_t kSceneState = 0x26;

/** "T1 2, T2 0, T3 0, T4 2 patterns, 1 scene": the saved (valid) patterns and scenes (ASCII: Windows consoles). */
inline std::string summary (const ProjectData& p)
{
    std::string s;
    for (int t = 1; t <= 4; ++t)
    {
        int n = 0;
        for (std::uint8_t pat = 1; pat <= 16; ++pat)
            n += blockValue (p, item::track (t), param::patternState, pat) == 3 ? 1 : 0;
        s += (t > 1 ? ", T" : "T") + std::to_string (t) + " " + std::to_string (n);
    }
    int scenes = 0;
    for (std::uint8_t sc = 1; sc <= 16; ++sc)
        scenes += blockValue (p, item::scene, kSceneState, sc) == 3 ? 1 : 0;
    return s + " patterns, " + std::to_string (scenes) + (scenes == 1 ? " scene" : " scenes");
}

// ---------------------------------------------------------------- JSON

namespace json
{
    inline std::string str (const std::string& s)
    {
        std::string o = "\"";
        for (char c : s)
        {
            if (c == '"' || c == '\\')
                o += '\\';
            o += c;
        }
        return o + "\"";
    }

    inline std::string value (int v) { return v == kUnreadable ? "null" : std::to_string (v); }

    template <typename T>
    std::string list (const std::vector<T>& v)
    {
        std::string o = "[";
        for (std::size_t i = 0; i < v.size(); ++i)
            o += (i ? "," : "") + value (static_cast<int> (v[i]));
        return o + "]";
    }
}

inline std::string toJson (const ProjectData& p, const std::string& firmware)
{
    std::string o = "{\"format\":\"keystep-pro-project\",\"version\":1,\"firmware\":" + json::str (firmware)
                    + ",\"project\":" + std::to_string (p.project) + ",\"name\":null,\"saved\":" + json::str (p.saved)
                    + ",\"data\":[";
    for (std::size_t i = 0; i < p.data.size(); ++i)
    {
        const auto& b = p.data[i];
        o += (i ? ",\n" : "\n") + std::string ("[") + std::to_string (b.item) + "," + std::to_string (b.param) + ","
             + json::list (b.path) + "," + json::list (b.values) + "]";
    }
    return o + "]}";
}

inline std::string toJson (const BackupData& b)
{
    std::string o = "{\"format\":\"keystep-pro-backup\",\"version\":1,\"firmware\":" + json::str (b.firmware)
                    + ",\"saved\":" + json::str (b.saved) + ",\"globals\":";
    if (b.globals.empty())
    {
        o += "null";
    }
    else
    {
        o += "{";
        for (std::size_t i = 0; i < b.globals.size(); ++i)
            o += (i ? "," : "") + json::str (std::to_string (b.globals[i].id)) + ":" + json::value (b.globals[i].value);
        o += "}";
    }
    o += ",\"projects\":[";
    for (std::size_t i = 0; i < b.projects.size(); ++i)
        o += (i ? ",\n" : "\n") + toJson (b.projects[i], b.firmware);
    return o + "]}\n";
}

} // namespace ksp
