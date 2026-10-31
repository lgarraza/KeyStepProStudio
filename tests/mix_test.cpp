// mix_test: Source/Mix/Limiter.h (the master limiter) and Transport.h (the KeyStep as master clock). No JUCE.
#include <cmath>
#include <cstdio>
#include <vector>
#include "Mix/Limiter.h"
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
    // 1. Limiter: a quiet signal passes unchanged (after the look-ahead), a loud one never goes above the ceiling, +9 dB boosts
    {
        mix::Limiter lim;
        lim.prepare (kRate);
        lim.setCeilingDb (-1.0f);
        const float ceiling = std::pow (10.0f, -1.0f / 20.0f);

        std::vector<float> l (48000), r (48000);
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.2f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / (float) kRate);
        const auto ref = l;
        for (std::size_t i = 0; i < l.size(); i += kBlock)
            lim.processStereo (l.data() + i, r.data() + i, (int) std::min<std::size_t> (kBlock, l.size() - i));
        bool same = true;
        const int lat = lim.latency();
        for (std::size_t i = (std::size_t) lat; i < l.size(); ++i)
            same = same && std::abs (l[i] - ref[i - (std::size_t) lat]) < 1.0e-6f;
        CHECK (1, same && lat > 0);

        lim.reset();
        lim.takeReductionDb();
        for (std::size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 1.8f * std::sin (2.0f * 3.14159265f * 110.0f * (float) i / (float) kRate);
        for (std::size_t i = 0; i < l.size(); i += kBlock)
            lim.processStereo (l.data() + i, r.data() + i, (int) std::min<std::size_t> (kBlock, l.size() - i));
        CHECK (1, peakOf (l, 0, l.size()) <= ceiling + 1.0e-6f && peakOf (l, 24000, l.size()) > 0.5f);   // limited, not silenced
        CHECK (1, lim.takeReductionDb() < -3.0f);

        // a short loud burst, then the gain comes back (release), a quiet signal ends at its own level
        lim.reset();
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        for (std::size_t i = 1000; i < 1200; ++i)
            l[i] = r[i] = 4.0f;
        for (std::size_t i = 4000; i < l.size(); ++i)
            l[i] = r[i] = 0.1f;
        for (std::size_t i = 0; i < l.size(); i += kBlock)
            lim.processStereo (l.data() + i, r.data() + i, (int) std::min<std::size_t> (kBlock, l.size() - i));
        CHECK (1, peakOf (l, 0, l.size()) <= ceiling + 1.0e-6f);
        CHECK (1, std::abs (l[l.size() - 1] - 0.1f) < 0.01f);

        mix::Limiter boost;
        boost.prepare (kRate);
        boost.setGainDb (9.0f);
        std::vector<float> bl (24000, 0.05f), br (24000, 0.05f);
        for (std::size_t i = 0; i < bl.size(); i += kBlock)
            boost.processStereo (bl.data() + i, br.data() + i, (int) std::min<std::size_t> (kBlock, bl.size() - i));
        CHECK (1, std::abs (bl.back() - 0.05f * std::pow (10.0f, 9.0f / 20.0f)) < 1.0e-4f);
    }

    // 2. Transport: the tempo from the clock, Start / Continue / Stop, the position from the ticks
    {
        mix::Transport tr;
        CHECK (2, ! tr.playing() && tr.ticks() == 0 && tr.bpm() == 120.0);
        const double dt = 60.0 / (90.0 * 24.0);   // 90 BPM
        double t = 1.0;
        for (int i = 0; i < 200; ++i, t += dt)
            tr.message (0xF8, t);
        CHECK (2, std::abs (tr.bpm() - 90.0) < 0.1 && ! tr.playing() && tr.ticks() == 0);   // the clock runs, nothing plays: no position

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
