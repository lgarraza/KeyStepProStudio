// mix_test: Source/Mix/Transport.h (the KeyStep as master clock). No JUCE.
#include <cmath>
#include <cstdio>
#include <vector>
#include "Mix/Transport.h"

static int failures = 0;

#define CHECK(n, ...)                                                \
    do {                                                             \
        if (! (__VA_ARGS__)) { std::printf ("FAIL %d: %s\n", n, #__VA_ARGS__); ++failures; } \
    } while (0)

static constexpr double kRate = 48000.0;
static constexpr int kBlock = 256;

static float peakOf (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < to && i < v.size(); ++i)
        p = std::max (p, std::abs (v[i]));
    return p;
}

int main()
{
    // 2. Transport: the tempo from the clock, Start / Continue / Stop, the position from the ticks
    {
        mix::Transport tr;
        CHECK (2, ! tr.playing() && tr.ticks() == 0 && tr.bpm() == 120.0 && ! tr.hasTempo());
        const double dt = 60.0 / (90.0 * 24.0);   // 90 BPM
        double t = 1.0;
        for (int i = 0; i < 200; ++i, t += dt)
            tr.message (0xF8, t);
        CHECK (2, tr.hasTempo() && std::abs (tr.bpm() - 90.0) < 0.1 && ! tr.playing() && tr.ticks() == 0);   // the clock runs, nothing plays: no position

        tr.message (0xFA, t);   // Start
        CHECK (2, tr.playing() && tr.ticks() == 0 && tr.starts() == 1);
        for (int i = 0; i < 96 + 24 + 6; ++i, t += dt)
            tr.message (0xF8, t);
        CHECK (2, tr.ticks() == 126 && mix::Transport::positionOf (tr.ticks()) == mix::Position { 2, 2, 2 });
        tr.message (0xFC, t);   // Stop: the position stays
        CHECK (2, ! tr.playing() && tr.ticks() == 126 && tr.stops() == 1);
        tr.message (0xF8, t + dt);
        CHECK (2, tr.ticks() == 126);   // a clock while stopped does not move it
        tr.message (0xFB, t + 2 * dt);   // Continue: plays from where it was
        tr.message (0xF8, t + 3 * dt);
        CHECK (2, tr.playing() && tr.ticks() == 127 && tr.starts() == 2);
        tr.message (0xFA, t + 4 * dt);   // Start again: from the beginning
        CHECK (2, tr.ticks() == 0);

        CHECK (2, mix::Transport::positionOf (0) == mix::Position { 1, 1, 1 });
        CHECK (2, mix::Transport::positionOf (6) == mix::Position { 1, 1, 2 });
        CHECK (2, mix::Transport::positionOf (24) == mix::Position { 1, 2, 1 });
        CHECK (2, mix::Transport::positionOf (96) == mix::Position { 2, 1, 1 });
        CHECK (2, std::abs (mix::Transport::secondsOf (96, 120.0) - 2.0) < 1.0e-9);   // a bar at 120 BPM
    }

    std::printf (failures == 0 ? "mix_test: OK\n" : "mix_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
