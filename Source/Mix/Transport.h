/*
    Transport.h
    The KeyStep Pro is the clock of the Studio (CLAUDE.md): this follows its MIDI real-time messages. Clock (0xF8, 24 per quarter note) gives
    the tempo and, while playing, the position; Start (0xFA) rewinds to the beginning and plays, Continue (0xFB) plays from where it stopped,
    Stop (0xFC) stops. Pure C++17 + std, no JUCE. message() is called from the MIDI thread; everything else reads atomics from any thread.
*/

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace mix
{

/** A position in 4/4: bar, beat and sixteenth, all counted from 1. */
struct Position
{
    int bar = 1, beat = 1, sixteenth = 1;
    bool operator== (const Position& o) const noexcept { return bar == o.bar && beat == o.beat && sixteenth == o.sixteenth; }
};

class Transport
{
public:
    static constexpr int kClocksPerQuarter = 24;

    /** One real-time byte (status only): 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop; anything else is ignored. */
    void message (std::uint8_t status, double nowSeconds) noexcept
    {
        switch (status)
        {
            case 0xF8: clock (nowSeconds); break;
            case 0xFA:
                tickCount.store (0);
                playingNow.store (true);
                startCounter.fetch_add (1);
                break;
            case 0xFB:
                playingNow.store (true);
                startCounter.fetch_add (1);
                break;
            case 0xFC:
                playingNow.store (false);
                stopCounter.fetch_add (1);
                lastClock = 0.0;
                average = 0.0;
                break;
            default: break;
        }
    }

    bool playing() const noexcept { return playingNow.load(); }
    /** Clock ticks since the last Start (24 per quarter note); frozen while stopped. */
    long ticks() const noexcept { return tickCount.load(); }
    /** The tempo in BPM from the clock (120 until a clock arrives). */
    double bpm() const noexcept { return tempo.load(); }
    /** How many Start / Continue and Stop messages arrived: a watcher compares them with what it saw last. */
    std::uint32_t starts() const noexcept { return startCounter.load(); }
    std::uint32_t stops() const noexcept { return stopCounter.load(); }

    static Position positionOf (long ticks) noexcept
    {
        ticks = std::max (0L, ticks);
        return { static_cast<int> (ticks / (4 * kClocksPerQuarter)) + 1, static_cast<int> ((ticks / kClocksPerQuarter) % 4) + 1,
                 static_cast<int> ((ticks / (kClocksPerQuarter / 4)) % 4) + 1 };
    }

    /** The time of `ticks` at `bpm`, in seconds. */
    static double secondsOf (long ticks, double bpm) noexcept { return bpm > 0.0 ? static_cast<double> (ticks) * 60.0 / (bpm * kClocksPerQuarter) : 0.0; }

private:
    void clock (double now) noexcept
    {
        if (playingNow.load())
            tickCount.fetch_add (1);
        if (lastClock > 0.0)
        {
            const double dt = now - lastClock;
            if (dt > 0.2 || dt < 0.002)
                average = 0.0;   // a pause or a burst: start measuring again
            else
            {
                average = average == 0.0 ? dt : 0.95 * average + 0.05 * dt;
                tempo.store (60.0 / (kClocksPerQuarter * average));
            }
        }
        lastClock = now;
    }

    std::atomic<bool> playingNow { false };
    std::atomic<long> tickCount { 0 };
    std::atomic<double> tempo { 120.0 };
    std::atomic<std::uint32_t> startCounter { 0 }, stopCounter { 0 };
    double lastClock = 0.0, average = 0.0;   // MIDI thread only
};

} // namespace mix
