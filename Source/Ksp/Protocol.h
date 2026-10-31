/*
    Protocol.h
    SysEx frames of the Arturia KeyStep Pro (firmware 2.5.20), C++17 + std, no JUCE.

    The protocol is not published by Arturia. Everything here comes from the reverse-engineering notes of
    KSCON (https://github.com/canadianblaken/keystep-con, MIT: findings.md, mcc-params.md), checked by them on
    hardware with firmware 2.5.20. See docs/protocolo.md.

    Frame: F0 00 20 6B <dev> 42 <cmd> <project> <param> ... F7
      - dev 0x7F = all devices; 0x42 = KeyStep Pro product ID.
      - project 0 = globals / current, 1..16 = that project slot.
    Builders return the whole frame including F0 and F7. Parsers take a whole frame (F0 .. F7) and return
    std::nullopt when it is not the reply asked for.
*/

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ksp
{

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::array<std::uint8_t, 6> kPrefix { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42 };

namespace cmd
{
    inline constexpr std::uint8_t get = 0x01;          // reply 0x02 + ACK 0x1C
    inline constexpr std::uint8_t set = 0x02;
    inline constexpr std::uint8_t openSession = 0x05;  // copies a project from flash into the transfer buffer, no reply
    inline constexpr std::uint8_t commit = 0x06;       // writes the transfer buffer to flash and reloads the project
    inline constexpr std::uint8_t bulkGet = 0x0B;      // reply 0x0C
    inline constexpr std::uint8_t bulkSet = 0x0C;
    inline constexpr std::uint8_t ack = 0x1C;
    inline constexpr std::uint8_t getHigh = 0x31;      // GET for global IDs >= 128 (index = id - 128), reply 0x32
    inline constexpr std::uint8_t setHigh = 0x32;
}

namespace item
{
    inline constexpr std::uint8_t project = 0x78;
    inline constexpr std::uint8_t scene = 0x79;
    inline constexpr std::uint8_t trackControl = 0x7A;
    inline constexpr std::uint8_t track1 = 0x7B;   // also the drum track
    inline constexpr std::uint8_t track2 = 0x7C;
    inline constexpr std::uint8_t track3 = 0x7D;
    inline constexpr std::uint8_t track4 = 0x7E;

    /** Track 1..4 -> its item. */
    inline constexpr std::uint8_t track (int n) noexcept { return static_cast<std::uint8_t> (track1 + (n - 1)); }
}

/** Param IDs (MCC paramId column of mcc-params.md). Only the ones used so far. */
namespace param
{
    inline constexpr std::uint8_t globals = 0x41;          // item byte = global ID 1..158
    inline constexpr std::uint8_t trackState = 0x27;       // 3 = valid block
    inline constexpr std::uint8_t patternState = 0x28;
    inline constexpr std::uint8_t stepActive = 48;         // [pattern, 1, firstStep] x16
    inline constexpr std::uint8_t stepSkip = 49;
    inline constexpr std::uint8_t noteStep = 50;           // note slot -> 0-based step it belongs to (0x7F = empty slot)
    inline constexpr std::uint8_t seqSwing = 97;           // value - 25 = percent
    inline constexpr std::uint8_t seqStepCount = 98;       // length - 1 (15 = 16 steps)
    inline constexpr std::uint8_t seqRoot = 107;
    inline constexpr std::uint8_t seqScale = 108;
    inline constexpr std::uint8_t notePitch = 109;         // [pattern, voice 1..3, firstSlot 1..64] x16, 192 slots
    inline constexpr std::uint8_t noteGate = 110;
    inline constexpr std::uint8_t noteVelocity = 111;
}

inline Bytes frame (std::initializer_list<std::uint8_t> body)
{
    Bytes out (kPrefix.begin(), kPrefix.end());
    out.insert (out.end(), body.begin(), body.end());
    out.push_back (0xF7);
    return out;
}

inline bool isValidProject (int project) noexcept { return project >= 1 && project <= 16; }

// ---------------------------------------------------------------- requests

/** Universal identity request: the reply carries manufacturer, family, member and firmware version. */
inline Bytes identityRequest() { return { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }; }

/** GET of one global setting (ID 1..158). IDs >= 128 use the 0x31 variant with index id - 128. */
inline Bytes globalGet (int id)
{
    if (id >= 128)
        return frame ({ cmd::getHigh, 0x00, param::globals, static_cast<std::uint8_t> (id - 128) });
    return frame ({ cmd::get, 0x00, param::globals, static_cast<std::uint8_t> (id) });
}

/** Opens a read session on a project (1..16). Read-only: the device only copies flash to its buffer. */
inline Bytes openSession (int project) { return frame ({ cmd::openSession, static_cast<std::uint8_t> (project) }); }

/** Bulk GET of `count` consecutive values at an index path (1-based indices), inside a session. */
inline Bytes bulkGet (int project, std::uint8_t paramId, std::uint8_t itemId, const std::vector<std::uint8_t>& indices,
                      std::uint8_t count)
{
    Bytes out (kPrefix.begin(), kPrefix.end());
    out.push_back (cmd::bulkGet);
    out.push_back (static_cast<std::uint8_t> (project));
    out.push_back (paramId);
    out.push_back (static_cast<std::uint8_t> (indices.size()));
    out.push_back (itemId);
    out.insert (out.end(), indices.begin(), indices.end());
    out.push_back (count);
    out.push_back (0xF7);
    return out;
}

/** Bulk SET of consecutive values at an index path, inside a session (WRITES the transfer buffer; commit() stores it).
    Values are 7-bit: anything above 127 is a caller bug and is masked. The device does not answer. */
inline Bytes bulkSet (int project, std::uint8_t paramId, std::uint8_t itemId, const std::vector<std::uint8_t>& indices,
                      const std::vector<std::uint8_t>& values)
{
    Bytes out (kPrefix.begin(), kPrefix.end());
    out.push_back (cmd::bulkSet);
    out.push_back (static_cast<std::uint8_t> (project));
    out.push_back (paramId);
    out.push_back (static_cast<std::uint8_t> (indices.size()));
    out.push_back (itemId);
    out.insert (out.end(), indices.begin(), indices.end());
    out.push_back (static_cast<std::uint8_t> (values.size()));
    for (auto v : values)
        out.push_back (static_cast<std::uint8_t> (v & 0x7F));
    out.push_back (0xF7);
    return out;
}

/** Commit: writes the session's transfer buffer to the project's flash and reloads it (about 3 s, no reply).
    Stock 2.5.20 flips every scene and pattern state it was not sent (see Restore.h). */
inline Bytes commit (int project) { return frame ({ cmd::commit, static_cast<std::uint8_t> (project) }); }

/** True for the frames a restore of `project` may send: open session, bulk SET and commit of that project only. */
inline bool isWriteFor (const Bytes& f, int project) noexcept
{
    if (f.size() < 9 || f.back() != 0xF7 || ! isValidProject (project))
        return false;
    for (std::size_t i = 0; i < kPrefix.size(); ++i)
        if (f[i] != kPrefix[i])
            return false;
    const auto c = f[6];
    return (c == cmd::openSession || c == cmd::bulkSet || c == cmd::commit) && f[7] == project;
}

/** True only for requests that cannot change the device: identity, GET, bulk GET and opening a read session.
    The phase 0 probe refuses to send anything else. */
inline bool isReadOnlyRequest (const Bytes& f) noexcept
{
    if (f == identityRequest())
        return true;
    if (f.size() < 8 || f.back() != 0xF7)
        return false;
    for (std::size_t i = 0; i < kPrefix.size(); ++i)
        if (f[i] != kPrefix[i])
            return false;
    const auto c = f[6];
    return c == cmd::get || c == cmd::getHigh || c == cmd::bulkGet || c == cmd::openSession;
}

// ---------------------------------------------------------------- replies

inline bool hasPrefix (const Bytes& f) noexcept
{
    if (f.size() < kPrefix.size() + 2 || f.back() != 0xF7)
        return false;
    for (std::size_t i = 1; i < 4; ++i)   // F0 00 20 6B; the device byte may be its own ID
        if (f[i] != kPrefix[i])
            return false;
    return f[5] == kPrefix[5];
}

inline bool isAck (const Bytes& f) noexcept { return hasPrefix (f) && f[6] == cmd::ack; }

struct Identity
{
    std::array<std::uint8_t, 2> family {};
    std::array<std::uint8_t, 2> member {};
    std::array<std::uint8_t, 4> version {};   // as sent: build (low 7 bits: 2085 -> 0x25), patch, minor, major

    int major() const noexcept { return version[3]; }
    int minor() const noexcept { return version[2]; }
    int patch() const noexcept { return version[1]; }
    std::string versionString() const
    {
        return std::to_string (major()) + "." + std::to_string (minor()) + "." + std::to_string (patch());
    }
    /** The only firmware this code was checked against. Writing to anything else stays blocked. */
    bool isKnownFirmware() const noexcept { return major() == 2 && minor() == 5 && patch() == 20; }
};

/** F0 7E <dev> 06 02 00 20 6B <fam fam> <mem mem> <v v v v> F7 */
inline std::optional<Identity> parseIdentity (const Bytes& f)
{
    if (f.size() != 17 || f[0] != 0xF0 || f[1] != 0x7E || f[3] != 0x06 || f[4] != 0x02 || f[5] != 0x00
        || f[6] != 0x20 || f[7] != 0x6B || f[16] != 0xF7)
        return std::nullopt;
    Identity id;
    id.family = { f[8], f[9] };
    id.member = { f[10], f[11] };
    id.version = { f[12], f[13], f[14], f[15] };
    return id;
}

/** Reply to globalGet(id): F0 00 20 6B dev 42 02|32 00 41 <idx> <value> F7 */
inline std::optional<int> parseGlobal (const Bytes& f, int id)
{
    if (! hasPrefix (f) || f.size() != 12 || f[7] != 0x00 || f[8] != param::globals)
        return std::nullopt;
    const bool high = id >= 128;
    if (f[6] != (high ? cmd::setHigh : cmd::set))
        return std::nullopt;
    if ((f[9] & 0x7F) != ((high ? id - 128 : id) & 0x7F))
        return std::nullopt;
    return f[10];
}

/** Reply to bulkGet: F0 .. 42 0C <project> <param> <k> <item> <idx x k> <count> <values x count> F7 */
inline std::optional<Bytes> parseBulk (const Bytes& f, int project, std::uint8_t paramId, std::uint8_t itemId,
                                       const std::vector<std::uint8_t>& indices, std::uint8_t count)
{
    const std::size_t k = indices.size();
    if (! hasPrefix (f) || f.size() != 13 + k + count || f[6] != cmd::bulkSet || f[7] != project || f[8] != paramId
        || f[9] != k || f[10] != itemId)
        return std::nullopt;
    for (std::size_t i = 0; i < k; ++i)
        if (f[11 + i] != indices[i])
            return std::nullopt;
    if (f[11 + k] != count)
        return std::nullopt;
    return Bytes (f.begin() + static_cast<std::ptrdiff_t> (12 + k), f.end() - 1);
}

/** Like parseBulk, but keeps a reply whose values were cut short. The device sends stored bytes unmasked: a value
    0xF7 ends the SysEx early and 0xF8..0xFF are split off as real-time bytes (KSCON, 6-device.js). The caller re-reads
    such a block value by value when the result has fewer than `count` values. */
inline std::optional<Bytes> parseBulkValues (const Bytes& f, int project, std::uint8_t paramId, std::uint8_t itemId,
                                             const std::vector<std::uint8_t>& indices, std::uint8_t count)
{
    const std::size_t k = indices.size();
    if (! hasPrefix (f) || f.size() < 13 + k || f.size() > 13 + k + count || f[6] != cmd::bulkSet || f[7] != project
        || f[8] != paramId || f[9] != k || f[10] != itemId)
        return std::nullopt;
    for (std::size_t i = 0; i < k; ++i)
        if (f[11 + i] != indices[i])
            return std::nullopt;
    if (f[11 + k] != count)
        return std::nullopt;
    return Bytes (f.begin() + static_cast<std::ptrdiff_t> (12 + k), f.end() - 1);
}

// ---------------------------------------------------------------- names

inline const char* scaleName (int v)
{
    static const char* names[] { "Chromatic", "Major", "Minor", "Dorian", "Mixolydian", "Harmonic minor", "Blues",
                                 "(unused)", "User 1", "User 2" };
    return v >= 0 && v < 10 ? names[v] : "?";
}

inline const char* pitchClassName (int pc)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

inline std::string noteName (int midi)
{
    if (midi < 0 || midi > 127)
        return "-";
    return std::string (pitchClassName (midi)) + std::to_string (midi / 12 - 2);   // Arturia: middle C (60) = C3
}

inline std::string hex (const Bytes& b)
{
    static const char* digits = "0123456789ABCDEF";
    std::string s;
    for (auto v : b)
    {
        if (! s.empty())
            s += ' ';
        s += digits[v >> 4];
        s += digits[v & 15];
    }
    return s;
}

} // namespace ksp
