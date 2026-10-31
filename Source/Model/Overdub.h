/*
    Overdub.h
    Real-time recording onto a Pattern (Pattern.h) from the KeyStep's MIDI out: notes are added, nothing is removed.
    C++17 + std, no JUCE; fed one raw MIDI message at a time, in arrival order, on one thread.

    Position comes from MIDI clock: Start (FA) resets it, each clock (F8) is one tick, 24 per quarter note
    (`clocksPerStep` 6 = 1/16 steps). A note-on goes to the nearest step, modulo the pattern length, with its velocity;
    gate, shift and randomness come from the pattern's defaults (the gate encoding is not mapped yet). A pitch that is
    already on that step is skipped: that is the KeyStep's own sequencer playing the stored notes back, or the same
    note played again on a later pass.
*/

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "Pattern.h"

namespace ksp::model
{

class Overdub
{
public:
    /** `channel` 0..15: only note-ons on that channel are recorded. */
    Overdub (int clocksPerStepIn, int channelIn) : clocksPerStep (std::max (1, clocksPerStepIn)), channel (channelIn) {}

    /** One MIDI message, status byte first. Returns true when a note was added to `pt`. */
    bool handle (Pattern& pt, const std::uint8_t* m, std::size_t size)
    {
        if (size == 0)
            return false;
        switch (m[0])
        {
            case 0xFA: running = true; clocks = -1; return false;   // the first clock after Start is tick 0
            case 0xFB: running = true; return false;
            case 0xFC: running = false; return false;
            case 0xF8: if (running) ++clocks; return false;
            default: break;
        }
        if (! running || size < 3 || (m[0] & 0xF0) != 0x90 || m[2] == 0 || (m[0] & 0x0F) != channel)
            return false;

        auto& step = pt.steps[static_cast<std::size_t> (stepAt (pt.length))];
        const int pitch = m[1];
        if (std::any_of (step.notes.begin(), step.notes.end(), [=] (const Note& n) { return n.pitch == pitch; }))
            return false;
        int total = 0;
        for (const auto& s : pt.steps)
            total += static_cast<int> (s.notes.size());
        if (static_cast<int> (step.notes.size()) >= kMaxStepNotes || total >= kSlots)
        {
            ++dropped;
            return false;
        }
        Note n = pt.defaults;
        n.pitch = pitch;
        n.velocity = m[2];
        if (n.gate == 0) n.gate = 64;
        step.notes.push_back (n);
        step.on = true;
        ++added;
        return true;
    }

    /** The step a note played now would land on. */
    int stepAt (int length) const
    {
        const long c = std::max (0L, clocks);
        return static_cast<int> ((c + clocksPerStep / 2) / clocksPerStep % std::max (1, length));
    }

    bool isRunning() const noexcept { return running; }

    int added = 0, dropped = 0;   // notes recorded / refused because the step (16) or the pattern (192) was full

private:
    int clocksPerStep, channel;
    bool running = false;
    long clocks = -1;
};

} // namespace ksp::model
