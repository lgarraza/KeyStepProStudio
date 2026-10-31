/*
    Restore.h
    Putting a project from a backup file back on a KeyStep Pro: reading the JSON (Backup.h's format, KSCON's
    "keystep-pro-backup" / "keystep-pro-project" v1), the order of the bulk SETs, and checking the read-back.
    C++17 + std, no JUCE: the device is reached through callbacks, so tests can use a fake one.

    Ported from KSCON's storeProject (web/src/6-device.js, MIT), verified there on hardware (2026-10-20):
      - Every value of the recall plan is sent, scene / track / pattern / project states LAST. Stock 2.5.20's commit
        flips every state it was not sent (valid <-> default), so sending all of them makes it flip nothing.
      - A value the backup could not read (null) is not sent: the slot keeps what it had there.
      - A chord with an unreadable byte (a note below its root) is not sent at all: SysEx carries 7 bits, so it
        cannot be written either. It is reported.
      - Empty drum slots in patterns marked valid are sent as the device stores them (127 everywhere), otherwise
        every slot counts as used.
      - Read-back: a track state written as default (2) reads valid (3), and params 55 / 56 past the last track are
        not stored; both are accepted.
    The caller opens the session, sends, commits (about 3 s) and reads the project again with readProject().
*/

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include "Backup.h"

namespace ksp
{

// ---------------------------------------------------------------- minimal JSON reader (enough for backup files)

namespace json
{
    struct Value
    {
        enum Type { null, number, string, array, object, boolean } type = null;
        double num = 0;
        std::string str;
        std::vector<Value> items;                              // array
        std::vector<std::pair<std::string, Value>> fields;     // object, in file order

        const Value* get (const std::string& key) const
        {
            for (const auto& f : fields)
                if (f.first == key)
                    return &f.second;
            return nullptr;
        }
        bool integral = false;                                 // a number written without '.' or exponent
        bool isInt() const noexcept { return type == number && integral; }
    };

    class Reader
    {
    public:
        explicit Reader (const std::string& text) : s (text) {}

        std::optional<Value> parse()
        {
            Value v;
            if (! value (v, 0))
                return std::nullopt;
            space();
            return i == s.size() ? std::optional<Value> (std::move (v)) : std::nullopt;
        }

    private:
        const std::string& s;
        std::size_t i = 0;

        void space() { while (i < s.size() && std::isspace (static_cast<unsigned char> (s[i]))) ++i; }
        bool word (const char* w)
        {
            const std::string t (w);
            if (s.compare (i, t.size(), t) != 0)
                return false;
            i += t.size();
            return true;
        }
        bool text (std::string& out)
        {
            if (s[i] != '"')
                return false;
            for (++i; i < s.size(); ++i)
            {
                if (s[i] == '"') { ++i; return true; }
                if (s[i] == '\\')
                {
                    if (++i >= s.size())
                        return false;
                    const char c = s[i];
                    if (c == 'u')   // names are ASCII; anything else becomes '?'
                    {
                        if (i + 4 >= s.size())
                            return false;
                        const auto code = std::strtol (s.substr (i + 1, 4).c_str(), nullptr, 16);
                        out += code < 128 ? static_cast<char> (code) : '?';
                        i += 4;
                    }
                    else
                        out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' ? '\b' : c == 'f' ? '\f' : c;
                }
                else
                    out += s[i];
            }
            return false;
        }
        bool value (Value& v, int depth)
        {
            space();
            if (i >= s.size() || depth > 32)
                return false;
            const char c = s[i];
            if (c == '{')
            {
                v.type = Value::object;
                ++i;
                space();
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                for (;;)
                {
                    space();
                    std::string key;
                    if (i >= s.size() || ! text (key))
                        return false;
                    space();
                    if (i >= s.size() || s[i++] != ':')
                        return false;
                    Value f;
                    if (! value (f, depth + 1))
                        return false;
                    v.fields.emplace_back (std::move (key), std::move (f));
                    space();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == '}') { ++i; return true; }
                    return false;
                }
            }
            if (c == '[')
            {
                v.type = Value::array;
                ++i;
                space();
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                for (;;)
                {
                    Value e;
                    if (! value (e, depth + 1))
                        return false;
                    v.items.push_back (std::move (e));
                    space();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == ']') { ++i; return true; }
                    return false;
                }
            }
            if (c == '"')
            {
                v.type = Value::string;
                return text (v.str);
            }
            if (word ("null")) { v.type = Value::null; return true; }
            if (word ("true")) { v.type = Value::boolean; v.num = 1; return true; }
            if (word ("false")) { v.type = Value::boolean; return true; }
            char* end = nullptr;
            v.num = std::strtod (s.c_str() + i, &end);
            if (end == s.c_str() + i)
                return false;
            v.type = Value::number;
            const auto len = static_cast<std::size_t> (end - s.c_str()) - i;
            v.integral = s.substr (i, len).find_first_of (".eE") == std::string::npos;
            i += len;
            return true;
        }
    };
}

// ---------------------------------------------------------------- backup / project files

struct LoadResult
{
    std::optional<BackupData> backup;
    std::string error;
};

/** The project's blocks must be the recall plan's, in order, with 0..127 or kUnreadable values. Empty = fine. */
inline std::string checkProject (const ProjectData& p)
{
    const auto plan = plan::expandPlan();
    if (! isValidProject (p.project))
        return "project number " + std::to_string (p.project) + " is not 1..16";
    if (p.data.size() != plan.size())
        return "project " + std::to_string (p.project) + " has " + std::to_string (p.data.size()) + " blocks, expected "
               + std::to_string (plan.size());
    for (std::size_t n = 0; n < plan.size(); ++n)
    {
        const auto& b = p.data[n];
        const auto& r = plan[n];
        bool ok = b.item == r.item && b.param == r.param && b.path == r.path && b.values.size() == r.count;
        for (auto v : b.values)
            ok = ok && (v == kUnreadable || (v >= 0 && v <= 127));
        if (! ok)
            return "project " + std::to_string (p.project) + ": block " + std::to_string (n + 1)
                   + " does not match the recall plan";
    }
    return {};
}

namespace detail
{
    inline std::optional<ProjectData> projectFrom (const json::Value& f, std::string& error)
    {
        const auto* fmt = f.get ("format");
        const auto* num = f.get ("project");
        const auto* data = f.get ("data");
        if (fmt == nullptr || fmt->str != "keystep-pro-project" || num == nullptr || ! num->isInt() || data == nullptr
            || data->type != json::Value::array)
        {
            error = "not a KeyStep Pro project";
            return std::nullopt;
        }
        ProjectData p;
        p.project = static_cast<int> (num->num);
        if (const auto* saved = f.get ("saved"))
            p.saved = saved->str;
        p.data.reserve (data->items.size());
        for (const auto& e : data->items)
        {
            if (e.type != json::Value::array || e.items.size() != 4 || ! e.items[0].isInt() || ! e.items[1].isInt()
                || e.items[2].type != json::Value::array || e.items[3].type != json::Value::array)
            {
                error = "project " + std::to_string (p.project) + ": a block is not [item, param, [path], [values]]";
                return std::nullopt;
            }
            Block b { static_cast<std::uint8_t> (e.items[0].num), static_cast<std::uint8_t> (e.items[1].num), {}, {} };
            for (const auto& x : e.items[2].items)
                b.path.push_back (static_cast<std::uint8_t> (x.num));
            for (const auto& x : e.items[3].items)
                b.values.push_back (x.type == json::Value::null ? kUnreadable : x.isInt() ? static_cast<int> (x.num) : 999);
            p.data.push_back (std::move (b));
        }
        error = checkProject (p);
        return error.empty() ? std::optional<ProjectData> (std::move (p)) : std::nullopt;
    }
}

/** A backup file, or a single project file (returned as a backup with one project and no globals). */
inline LoadResult fromJson (const std::string& text)
{
    const auto root = json::Reader (text).parse();
    if (! root || root->type != json::Value::object)
        return { std::nullopt, "not a JSON file" };

    BackupData b;
    std::string error;
    const auto* fmt = root->get ("format");
    if (fmt != nullptr && fmt->str == "keystep-pro-project")
    {
        auto p = detail::projectFrom (*root, error);
        if (! p)
            return { std::nullopt, error };
        b.saved = p->saved;
        b.projects.push_back (std::move (*p));
        return { std::move (b), {} };
    }
    const auto* version = root->get ("version");
    const auto* projects = root->get ("projects");
    if (fmt == nullptr || fmt->str != "keystep-pro-backup" || version == nullptr || ! version->isInt() || static_cast<int> (version->num) != 1
        || projects == nullptr || projects->type != json::Value::array)
        return { std::nullopt, "not a KeyStep Pro backup file" };
    if (const auto* saved = root->get ("saved"))
        b.saved = saved->str;
    if (const auto* fw = root->get ("firmware"))
        b.firmware = fw->str;
    if (const auto* g = root->get ("globals"); g != nullptr && g->type == json::Value::object)
        for (const auto& [id, v] : g->fields)
            b.globals.push_back ({ std::atoi (id.c_str()), v.type == json::Value::null ? kUnreadable : static_cast<int> (v.num) });
    std::set<int> seen;
    for (const auto& f : projects->items)
    {
        auto p = detail::projectFrom (f, error);
        if (! p)
            return { std::nullopt, error };
        if (! seen.insert (p->project).second)
            return { std::nullopt, "the backup lists project " + std::to_string (p->project) + " twice" };
        b.projects.push_back (std::move (*p));
    }
    return { std::move (b), {} };
}

// ---------------------------------------------------------------- what is sent

inline constexpr std::uint8_t kChordParam = 0x57;
inline constexpr std::uint8_t kEmpty = 0x7F;

/** Address of one value inside the project's blocks (the block's last index counts up), or nullptr. */
inline int* cell (ProjectData& p, std::uint8_t it, std::uint8_t prm, const std::vector<std::uint8_t>& path)
{
    for (auto& b : p.data)
    {
        if (b.item != it || b.param != prm || b.path.size() != path.size()
            || ! std::equal (path.begin(), path.end() - 1, b.path.begin()))
            continue;
        const int first = b.path.back(), last = path.back();
        if (last >= first && last < first + static_cast<int> (b.values.size()))
            return &b.values[static_cast<std::size_t> (last - first)];
    }
    return nullptr;
}

/** Empty drum slots of track 1's valid patterns as the device writes them (127 in lane, gate, velocity, shift and
    random). Untouched patterns carry other values there, harmless while a pattern is default. Returns the values
    changed. */
inline int fixEmptyDrumSlots (ProjectData& p)
{
    constexpr std::uint8_t drumStep = 0x36;
    constexpr std::uint8_t drumNote[] { 0x75, 0x76, 0x77, 0x78, 0x79 };
    int n = 0;
    for (std::uint8_t pat = 1; pat <= 16; ++pat)
    {
        const auto* state = cell (p, item::track1, param::patternState, { pat });
        if (state == nullptr || *state != 3)
            continue;
        for (int s = 0; s < 192; ++s)
        {
            const std::vector<std::uint8_t> path { pat, static_cast<std::uint8_t> (s / 64 + 1), static_cast<std::uint8_t> (s % 64 + 1) };
            const auto* step = cell (p, item::track1, drumStep, path);
            if (step == nullptr || *step != kEmpty)
                continue;
            for (auto prm : drumNote)
                if (auto* v = cell (p, item::track1, prm, path); v != nullptr && *v != kUnreadable && *v != kEmpty)
                {
                    *v = kEmpty;
                    ++n;
                }
        }
    }
    return n;
}

inline bool isStateParam (std::uint8_t prm) noexcept { return prm >= 0x25 && prm <= 0x28; }

/** Tracks whose chord has an unreadable byte (a note below its root): not sent, reported. */
inline std::set<std::uint8_t> unsendableChords (const ProjectData& p)
{
    std::set<std::uint8_t> out;
    for (const auto& b : p.data)
        if (b.param == kChordParam && std::find (b.values.begin(), b.values.end(), kUnreadable) != b.values.end())
            out.insert (b.item);
    return out;
}

inline std::string trackLabel (std::uint8_t it)
{
    return it == item::trackControl ? "control track" : "track " + std::to_string (it - item::track1 + 1);
}

struct WriteRequest
{
    std::uint8_t item;
    std::uint8_t param;
    std::vector<std::uint8_t> path;
    std::vector<std::uint8_t> values;
};

struct WritePlan
{
    std::vector<WriteRequest> requests;   // in sending order
    std::vector<std::string> notes;       // what is left out, for the user
    int drumFixed = 0;
};

/** The bulk SETs that put `p` back, states last. `p` gets the drum fix applied (it is what the read-back is
    compared with). With `before` (the slot as it was just read, same recall-plan layout) only the blocks that differ
    from it are sent, plus every state block (a commit flips the valid / default mark of each state that is not sent
    again): the same slot content after the commit, in a fraction of the time. */
inline WritePlan writePlan (ProjectData& p, const ProjectData* before = nullptr)
{
    WritePlan w;
    w.drumFixed = fixEmptyDrumSlots (p);
    const auto badChords = unsendableChords (p);
    for (auto it : badChords)
        w.notes.push_back (trackLabel (it) + " chord not sent: it has a note below its root, which SysEx cannot carry. "
                           "Enter it again on the device.");
    int unreadable = 0;
    const bool diffOnly = before != nullptr && before->data.size() == p.data.size();
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t n = 0; n < p.data.size(); ++n)
        {
            const auto& b = p.data[n];
            if (isStateParam (b.param) != (pass == 1) || (b.param == kChordParam && badChords.count (b.item) != 0))
                continue;
            if (diffOnly && pass == 0)
            {
                const auto& o = before->data[n];
                if (o.item == b.item && o.param == b.param && o.path == b.path && o.values == b.values)
                    continue;
            }
            if (std::find (b.values.begin(), b.values.end(), kUnreadable) == b.values.end())
            {
                w.requests.push_back ({ b.item, b.param, b.path, std::vector<std::uint8_t> (b.values.begin(), b.values.end()) });
                continue;
            }
            for (std::size_t j = 0; j < b.values.size(); ++j)
            {
                if (b.values[j] == kUnreadable) { ++unreadable; continue; }
                auto path = b.path;
                path.back() = static_cast<std::uint8_t> (path.back() + j);
                w.requests.push_back ({ b.item, b.param, path, { static_cast<std::uint8_t> (b.values[j]) } });
            }
        }
    if (unreadable > 0)
        w.notes.push_back (std::to_string (unreadable) + " value(s) the backup could not read are not sent: the slot keeps "
                           "what it has there.");
    return w;
}

// ---------------------------------------------------------------- read-back

/** Differences between what was written (`written`, after writePlan) and what reads back. Empty = all as written. The note
    values (pitch, gate, velocity, shift, random) of a sequencer slot that is empty on both sides (step = 127) are not
    compared: the device keeps its own defaults there (7, 100, 49, 100) whatever was sent, and they play nothing. */
inline std::vector<std::string> compareReadBack (const ProjectData& written, const ProjectData& back)
{
    std::vector<std::string> out;
    if (written.data.size() != back.data.size())
        return { "the read-back has " + std::to_string (back.data.size()) + " blocks, expected "
                 + std::to_string (written.data.size()) };
    const auto badChords = unsendableChords (written);
    constexpr std::uint8_t kSlotStep = 0x32, kNoteFirst = 0x6D, kNoteLast = 0x71;
    auto stepBlock = [kSlotStep] (const ProjectData& p, const Block& note) -> const Block* {
        for (const auto& b : p.data)
            if (b.item == note.item && b.param == kSlotStep && b.path == note.path)
                return &b;
        return nullptr;
    };
    for (std::size_t n = 0; n < written.data.size(); ++n)
    {
        const auto& w = written.data[n];
        const auto& r = back.data[n];
        if (w.param == kChordParam && badChords.count (w.item) != 0)
            continue;
        const Block* wStep = nullptr;
        const Block* rStep = nullptr;
        const bool isNote = w.item >= item::track1 && w.item <= item::track4 && w.param >= kNoteFirst && w.param <= kNoteLast;
        if (isNote)
        {
            wStep = stepBlock (written, w);
            rStep = stepBlock (back, w);
        }
        const bool unstored = w.item == item::project && w.path.size() == 1
                              && ((w.param == 55 && w.path[0] == 5) || (w.param == 56 && w.path[0] >= 4));
        if (unstored)
            continue;
        bool same = r.values.size() == w.values.size();
        for (std::size_t j = 0; same && j < w.values.size(); ++j)
        {
            const bool emptySlot = wStep != nullptr && rStep != nullptr && j < wStep->values.size() && j < rStep->values.size()
                                   && wStep->values[j] == kEmpty && rStep->values[j] == kEmpty;
            same = emptySlot || w.values[j] == kUnreadable || r.values[j] == w.values[j]
                   || (w.param == param::trackState && w.values[j] == 2 && r.values[j] == 3);
        }
        if (same)
            continue;
        std::string p, a, b;
        for (auto x : w.path) p += (p.empty() ? "" : ",") + std::to_string (x);
        for (auto x : r.values) a += (a.empty() ? "" : " ") + (x == kUnreadable ? std::string ("null") : std::to_string (x));
        for (auto x : w.values) b += (b.empty() ? "" : " ") + (x == kUnreadable ? std::string ("null") : std::to_string (x));
        out.push_back ("item " + std::to_string (w.item) + " param " + std::to_string (w.param) + " [" + p + "] reads " + a
                       + ", wrote " + b);
    }
    return out;
}

} // namespace ksp
