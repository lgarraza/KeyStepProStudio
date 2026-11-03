// Arpeggiator mode, octave and swing as read on the device (Source/Model/ArpSettings.h).
#include <cstdio>
#include "../Source/Model/ArpSettings.h"

using namespace ksp::model;

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main()
{
    ksp::ProjectData p;
    p.project = 16;
    for (const auto& r : ksp::plan::expandPlan())
        p.data.push_back ({ r.item, r.param, r.path, std::vector<int> (r.count, 0) });

    // values seen on the device (T2): factory 26 = Order, octave 0; 68 = Exclu +3; 20 = Exclu 0; 16 = Up 0; 28 = Poly 0
    auto set100 = [&] (int pat, int v) { *ksp::cell (p, ksp::item::track (2), ap::config, { static_cast<std::uint8_t> (pat) }) = v; };
    set100 (1, 68); set100 (2, 20); set100 (3, 16); set100 (4, 26); set100 (5, 28);
    CHECK (readArp (p, 2, 1).mode == 2 && readArp (p, 2, 1).octave == 3);
    CHECK (readArp (p, 2, 2).mode == 2 && readArp (p, 2, 2).octave == 0);
    CHECK (readArp (p, 2, 3).mode == 0 && readArp (p, 2, 4).mode == 5 && readArp (p, 2, 5).mode == 6);
    CHECK (! readArp (p, 1, 1).valid && ! readArp (p, 2, 17).valid);

    // swing: 53 % reads as 3 in item 0x78 p56 [1] (T2), 55 % as 5 in [2] (T3)
    *ksp::cell (p, ksp::item::project, ap::swing, { 1 }) = 3;
    *ksp::cell (p, ksp::item::project, ap::swing, { 2 }) = 5;
    CHECK (readArp (p, 2, 1).swing == 53 && readArp (p, 3, 1).swing == 55);

    ArpSettings a { true, 4, -1, 60 };
    CHECK (writeArp (p, 2, 3, a));
    const auto b = readArp (p, 2, 3);
    CHECK (b.mode == 4 && b.octave == -1 && b.swing == 60);
    CHECK (*ksp::cell (p, ksp::item::track (2), ap::config, { 3 }) == 8);   // 16 * 0 + 2 * 4
    CHECK (writeArp (p, 4, 1, { true, 99, 99, 99 }));   // clamped
    const auto d = readArp (p, 4, 1);
    CHECK (d.mode == 6 && d.octave == 3 && d.swing == 75);

    std::printf (failures == 0 ? "arp_test OK\n" : "arp_test FAILED\n");
    return failures == 0 ? 0 : 1;
}
