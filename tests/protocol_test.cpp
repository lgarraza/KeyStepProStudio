// protocol_test: Source/Ksp/Protocol.h against the frames KSCON captured on hardware (findings.md, firmware 2.5.20).
// No JUCE. Build: cmake --build build --target protocol_test && ctest --test-dir build
#include <cstdio>
#include "Ksp/Protocol.h"

using ksp::Bytes;

static int failures = 0;

#define CHECK(n, cond)                                               \
    do {                                                             \
        if (! (cond)) { std::printf ("FAIL %d: %s\n", n, #cond); ++failures; } \
    } while (0)

int main()
{
    // 1. Identity request and the reply of a 2.5.20 unit
    CHECK (1, ksp::identityRequest() == (Bytes { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }));
    const Bytes idReply { 0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x00, 0x20, 0x6B, 0x02, 0x00, 0x09, 0x00, 0x25, 0x14, 0x05, 0x02, 0xF7 };
    const auto id = ksp::parseIdentity (idReply);
    CHECK (1, id.has_value() && id->versionString() == "2.5.20" && id->isKnownFirmware());
    CHECK (1, ! ksp::parseIdentity (Bytes { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }));

    // 2. Global GET, low and high IDs, and their replies
    CHECK (2, ksp::globalGet (0x3D) == (Bytes { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x01, 0x00, 0x41, 0x3D, 0xF7 }));
    CHECK (2, ksp::globalGet (152) == (Bytes { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x31, 0x00, 0x41, 0x18, 0xF7 }));
    const Bytes globalReply { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x00, 0x41, 0x3D, 0x00, 0xF7 };
    CHECK (2, ksp::parseGlobal (globalReply, 0x3D) == 0);
    CHECK (2, ! ksp::parseGlobal (globalReply, 0x3E));
    const Bytes highReply { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x32, 0x00, 0x41, 0x98, 0x01, 0xF7 };   // idx bit 7 set
    CHECK (2, ksp::parseGlobal (Bytes { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x32, 0x00, 0x41, 0x18, 0x01, 0xF7 }, 152) == 1);
    CHECK (2, ksp::parseGlobal (Bytes (highReply.begin(), highReply.end()), 152) == 1);

    // 3. Session and bulk GET: track 1 chord byte n=1 of project 1 (findings.md example)
    CHECK (3, ksp::openSession (1) == (Bytes { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x05, 0x01, 0xF7 }));
    CHECK (3, ksp::bulkGet (1, 0x57, ksp::item::track1, { 1 }, 1)
                  == (Bytes { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0B, 0x01, 0x57, 0x01, 0x7B, 0x01, 0x01, 0xF7 }));
    const Bytes bulkReply { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 0x01, 0x57, 0x01, 0x7B, 0x01, 0x01, 0x40, 0xF7 };
    const auto v = ksp::parseBulk (bulkReply, 1, 0x57, ksp::item::track1, { 1 }, 1);
    CHECK (3, v && v->size() == 1 && (*v)[0] == 0x40);   // value at byte 13
    CHECK (3, ! ksp::parseBulk (bulkReply, 2, 0x57, ksp::item::track1, { 1 }, 1));
    CHECK (3, ! ksp::parseBulk (bulkReply, 1, 0x57, ksp::item::track1, { 2 }, 1));

    // 4. Bulk GET with a three-index path and 16 values
    Bytes many { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x0C, 0x03, 109, 0x03, 0x7C, 1, 1, 17, 16 };
    for (int i = 0; i < 16; ++i)
        many.push_back (static_cast<std::uint8_t> (60 + i));
    many.push_back (0xF7);
    const auto notes = ksp::parseBulk (many, 3, 109, ksp::item::track2, { 1, 1, 17 }, 16);
    CHECK (4, notes && notes->size() == 16 && notes->front() == 60 && notes->back() == 75);

    // 5. Only reads pass the probe's guard
    CHECK (5, ksp::isReadOnlyRequest (ksp::identityRequest()));
    CHECK (5, ksp::isReadOnlyRequest (ksp::globalGet (10)) && ksp::isReadOnlyRequest (ksp::globalGet (150)));
    CHECK (5, ksp::isReadOnlyRequest (ksp::openSession (4)));
    CHECK (5, ksp::isReadOnlyRequest (ksp::bulkGet (1, 98, ksp::item::track3, { 2 }, 1)));
    CHECK (5, ! ksp::isReadOnlyRequest (ksp::frame ({ ksp::cmd::set, 0x00, 0x41, 0x18, 0x01 })));
    CHECK (5, ! ksp::isReadOnlyRequest (ksp::frame ({ ksp::cmd::commit, 0x01 })));
    CHECK (5, ! ksp::isReadOnlyRequest (ksp::frame ({ ksp::cmd::bulkSet, 0x01, 0x57, 0x01, 0x7B, 0x01, 0x01, 0x40 })));

    // 6. Names
    CHECK (6, ksp::noteName (60) == "C3" && ksp::noteName (61) == "C#3" && ksp::noteName (128) == "-");
    CHECK (6, std::string (ksp::scaleName (2)) == "Minor" && std::string (ksp::pitchClassName (14)) == "D");

    std::printf (failures == 0 ? "protocol_test: OK\n" : "protocol_test: %d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
