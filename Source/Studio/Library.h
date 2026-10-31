/*
    Library.h
    The folder with the Studio projects (one file each, <id>.kspstudio) and a small index (index.json) so a list of 200 projects
    shows at once without parsing 200 files of about 700 KB. The files are the truth; the index is rebuilt from them when it is
    missing, unreadable or does not match the files. C++17 + std (std::filesystem), no JUCE.

    Writes are atomic (a temporary file, then a rename), so a crash never leaves a half-written project.
*/

#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "StudioProject.h"

namespace ksp::studio
{

namespace fs = std::filesystem;

/** What the list shows about a project. */
struct Entry
{
    std::string id, name, created, modified, notes;
    std::string summary;                  // "T1 4, T2 4, T3 4, T4 4 patterns, 8 scenes"
    std::vector<std::string> instruments;  // the plugin names of the four tracks that have one

    bool operator== (const Entry& o) const
    {
        return id == o.id && name == o.name && created == o.created && modified == o.modified && notes == o.notes
               && summary == o.summary && instruments == o.instruments;
    }
};

inline Entry entryOf (const StudioProject& p)
{
    Entry e { p.id, p.name, p.created, p.modified, p.notes, ksp::summary (p.ksp), {} };
    for (const auto& t : p.tracks)
        if (t.instrument)
            e.instruments.push_back (t.instrument->name);
    return e;
}

class Library
{
public:
    explicit Library (fs::path folderToUse) : folder (std::move (folderToUse)) {}

    const fs::path& path() const noexcept { return folder; }

    /** Files that could not be read the last time the index was rebuilt (name and why). */
    const std::vector<std::string>& problems() const noexcept { return skipped; }

    /** The projects, newest modification first. */
    std::vector<Entry> list()
    {
        if (! loadIndex())
            rebuildIndex();
        auto out = entries;
        std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) {
            return a.modified != b.modified ? a.modified > b.modified : a.name < b.name;
        });
        return out;
    }

    std::optional<StudioProject> load (const std::string& id, std::string& error) const
    {
        if (! validId (id))
        {
            error = "not a project id";
            return std::nullopt;
        }
        std::ifstream f (fileFor (id), std::ios::binary);
        if (! f)
        {
            error = "the project " + id + " is not in the library";
            return std::nullopt;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        auto r = fromJson (ss.str());
        if (! r.project)
            error = r.error;
        return std::move (r.project);
    }

    /** Saves `p` (a new id and creation date when it has none; the modification date is set to `now`). Empty = fine. */
    std::string save (StudioProject& p, const std::string& now = nowIso())
    {
        if (p.name.empty())
            return "the project needs a name";
        std::error_code ec;
        fs::create_directories (folder, ec);
        if (ec)
            return "cannot create " + folder.string() + ": " + ec.message();
        if (p.id.empty())
            p.id = newId();
        if (! validId (p.id))
            return "not a project id";
        if (p.created.empty())
            p.created = now;
        p.modified = now;

        const auto target = fileFor (p.id);
        auto temp = target;
        temp += ".tmp";
        {
            std::ofstream f (temp, std::ios::binary | std::ios::trunc);
            const auto text = toJson (p);
            if (! f || ! f.write (text.data(), static_cast<std::streamsize> (text.size())))
                return "cannot write " + temp.string();
        }
        fs::rename (temp, target, ec);
        if (ec)
        {
            fs::remove (temp, ec);
            return "cannot replace " + target.string();
        }
        if (! loadIndex())
            rebuildIndex();
        else
            putEntry (entryOf (p));
        return saveIndex();
    }

    /** A copy with a new id, `name` and fresh dates; returns the new id (empty and `error` set on failure). */
    std::string duplicate (const std::string& id, const std::string& name, std::string& error)
    {
        auto p = load (id, error);
        if (! p)
            return {};
        p->id.clear();
        p->created.clear();
        p->name = name;
        error = save (*p);
        return error.empty() ? p->id : std::string();
    }

    bool remove (const std::string& id)
    {
        if (! validId (id))
            return false;
        std::error_code ec;
        const bool gone = fs::remove (fileFor (id), ec);
        if (! loadIndex())
            rebuildIndex();
        else
        {
            entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const Entry& e) { return e.id == id; }), entries.end());
            saveIndex();
        }
        return gone;
    }

private:
    fs::path folder;
    std::vector<Entry> entries;
    std::vector<std::string> skipped;
    std::size_t skippedCount = 0;   // files the last rebuild could not read; kept in the index so a bad file does not force a rebuild each time

    static bool validId (const std::string& id)
    {
        return id.size() == 32 && std::all_of (id.begin(), id.end(), [] (char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    }

    fs::path fileFor (const std::string& id) const { return folder / (id + ".kspstudio"); }
    fs::path indexFile() const { return folder / "index.json"; }

    void putEntry (const Entry& e)
    {
        for (auto& x : entries)
            if (x.id == e.id)
            {
                x = e;
                return;
            }
        entries.push_back (e);
    }

    std::size_t projectFiles() const
    {
        std::size_t n = 0;
        std::error_code ec;
        for (fs::directory_iterator it (folder, ec), end; ! ec && it != end; it.increment (ec))
            if (it->path().extension() == ".kspstudio")
                ++n;
        return n;
    }

    /** The index in memory, from index.json; false when it is missing, unreadable or the folder holds another number of projects. */
    bool loadIndex()
    {
        std::ifstream f (indexFile(), std::ios::binary);
        if (! f)
            return false;
        std::stringstream ss;
        ss << f.rdbuf();
        const auto root = json::Reader (ss.str()).parse();
        const auto* list = root ? root->get ("projects") : nullptr;
        if (list == nullptr || list->type != json::Value::array)
            return false;
        entries.clear();
        for (const auto& v : list->items)
        {
            Entry e { detail::text (v, "id"), detail::text (v, "name"), detail::text (v, "created"), detail::text (v, "modified"),
                      detail::text (v, "notes"), detail::text (v, "summary"), {} };
            if (const auto* inst = v.get ("instruments"); inst != nullptr && inst->type == json::Value::array)
                for (const auto& n : inst->items)
                    e.instruments.push_back (n.str);
            if (! validId (e.id))
                return false;
            entries.push_back (std::move (e));
        }
        skippedCount = root->get ("skipped") != nullptr ? static_cast<std::size_t> (root->get ("skipped")->num) : 0;
        return entries.size() + skippedCount == projectFiles();
    }

    void rebuildIndex()
    {
        entries.clear();
        skipped.clear();
        skippedCount = 0;
        std::error_code ec;
        for (fs::directory_iterator it (folder, ec), end; ! ec && it != end; it.increment (ec))
        {
            if (it->path().extension() != ".kspstudio")
                continue;
            std::ifstream f (it->path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            auto r = fromJson (ss.str());
            if (! r.project || ! validId (r.project->id) || it->path().stem().string() != r.project->id)
                skipped.push_back (it->path().filename().string() + ": " + (r.project ? "the file name does not match its id" : r.error));
            else
                entries.push_back (entryOf (*r.project));
        }
        skippedCount = skipped.size();
        saveIndex();
    }

    std::string saveIndex() const
    {
        std::string o = "{\"version\":1,\"skipped\":" + std::to_string (skippedCount) + ",\"projects\":[";
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            o += (i ? ",\n" : "\n") + std::string ("{\"id\":") + esc (e.id) + ",\"name\":" + esc (e.name) + ",\"created\":" + esc (e.created)
                 + ",\"modified\":" + esc (e.modified) + ",\"notes\":" + esc (e.notes) + ",\"summary\":" + esc (e.summary)
                 + ",\"instruments\":[";
            for (std::size_t k = 0; k < e.instruments.size(); ++k)
                o += (k ? "," : "") + esc (e.instruments[k]);
            o += "]}";
        }
        o += "]}\n";
        std::error_code ec;
        fs::create_directories (folder, ec);
        const auto temp = indexFile().string() + ".tmp";
        {
            std::ofstream f (temp, std::ios::binary | std::ios::trunc);
            if (! f || ! f.write (o.data(), static_cast<std::streamsize> (o.size())))
                return "cannot write the library index";
        }
        fs::rename (temp, indexFile(), ec);
        return ec ? "cannot replace the library index" : std::string();
    }
};

} // namespace ksp::studio
