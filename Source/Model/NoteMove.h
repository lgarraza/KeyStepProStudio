/*
    Moving and copying notes inside a Pattern (the grid's drag): by steps and by semitones, a group at a time. No JUCE.
    All or nothing: if any note would leave the 64 steps / 0..127 or a step would pass 16 notes, the pattern stays as it was.
*/
#pragma once

#include <algorithm>
#include <vector>
#include "Pattern.h"

namespace ksp::model
{

struct NoteRef
{
    int step = 0, pitch = 0;
    bool operator< (const NoteRef& o) const { return step != o.step ? step < o.step : pitch < o.pitch; }
    bool operator== (const NoteRef& o) const { return step == o.step && pitch == o.pitch; }
};

/** Moves (or, with `copy`, copies) the notes of `sel` by dStep steps and dPitch semitones. A note landing on the same
    pitch of a step replaces the one there. Values (gate, velocity, shift, random) travel with the note. `moved` gets the
    new positions. Returns false, leaving `p` untouched, when it does not fit. */
inline bool moveNotes (Pattern& p, const std::vector<NoteRef>& sel, int dStep, int dPitch, bool copy, std::vector<NoteRef>* moved = nullptr)
{
    struct Item { NoteRef to; Note note; };
    std::vector<Item> items;
    for (const auto& r : sel)
    {
        if (r.step < 0 || r.step >= kSteps)
            continue;
        const auto& notes = p.steps[static_cast<std::size_t> (r.step)].notes;
        const auto it = std::find_if (notes.begin(), notes.end(), [&] (const Note& n) { return n.pitch == r.pitch; });
        if (it == notes.end())
            continue;
        const NoteRef to { r.step + dStep, r.pitch + dPitch };
        if (to.step < 0 || to.step >= kSteps || to.pitch < 0 || to.pitch > 127)
            return false;
        items.push_back ({ to, *it });
    }

    auto work = p;
    if (! copy)
        for (const auto& r : sel)
        {
            if (r.step < 0 || r.step >= kSteps)
                continue;
            auto& s = work.steps[static_cast<std::size_t> (r.step)];
            s.notes.erase (std::remove_if (s.notes.begin(), s.notes.end(), [&] (const Note& n) { return n.pitch == r.pitch; }), s.notes.end());
            s.on = ! s.notes.empty();
        }
    for (const auto& i : items)
    {
        auto& s = work.steps[static_cast<std::size_t> (i.to.step)];
        auto it = std::find_if (s.notes.begin(), s.notes.end(), [&] (const Note& n) { return n.pitch == i.to.pitch; });
        Note n = i.note;
        n.pitch = i.to.pitch;
        if (it != s.notes.end())
            *it = n;
        else if (static_cast<int> (s.notes.size()) >= kMaxStepNotes)
            return false;
        else
            s.notes.push_back (n);
        s.on = true;
    }
    p = std::move (work);
    if (moved != nullptr)
    {
        moved->clear();
        for (const auto& i : items)
            moved->push_back (i.to);
    }
    return true;
}

/** Same length and same steps (on, passes, notes with their values): what the grid edits. */
inline bool sameSteps (const Pattern& a, const Pattern& b)
{
    if (a.length != b.length)
        return false;
    for (std::size_t s = 0; s < a.steps.size(); ++s)
    {
        const auto &x = a.steps[s], &y = b.steps[s];
        if (x.on != y.on || x.passes != y.passes || x.notes.size() != y.notes.size())
            return false;
        for (std::size_t i = 0; i < x.notes.size(); ++i)
        {
            const auto &m = x.notes[i], &n = y.notes[i];
            if (m.pitch != n.pitch || m.gate != n.gate || m.velocity != n.velocity || m.shift != n.shift || m.random != n.random)
                return false;
        }
    }
    return true;
}

}  // namespace ksp::model
