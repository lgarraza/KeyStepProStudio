/*
    Styles.h
    Style presets for the role engine (Roles.h): techno, house and drum & bass. A style is data: a key, a progression, a
    drum groove for track 1 (in drum mode) and three RoleSpecs with their time division for tracks 2-4 (bass, chords,
    lead) and the tempo, written into the project (the KeyStep plays the project at it). Ambient stays in Ambient.h.
    Deterministic. C++17 + std, no JUCE.
*/

#pragma once

#include <cmath>
#include "Ambient.h"

namespace ksp::gen
{

struct TrackStyle
{
    RoleSpec spec;
    model::Division division = model::Division::sixteenth;
    bool poly = false;
};

struct Style
{
    std::string name, label, info;
    int bpm = 120;
    int root = 9, scale = 2;
    std::vector<Chord> progression;
    DrumSpec drums;
    TrackStyle tracks[3];   // tracks 2, 3, 4
};

inline const std::vector<Style>& styles()
{
    static const std::vector<Style> all = [] {
        std::vector<Style> v;
        {
            Style s;   // four on the floor, offbeat open hats, rolling sixteenth hats, a clap on 2 and 4
            s.name = "techno";
            s.label = "Techno";
            s.info = "F minor, 130 BPM: four on the floor, offbeat bass, minor stabs, a sparse lead.";
            s.bpm = 130;
            s.root = 5;
            s.scale = 2;
            s.progression = { { 0, { 0, 2, 4, 6 } }, { 0, { 0, 2, 4, 6 } }, { 5, { 0, 2, 4, 6 } }, { 6, { 0, 2, 4 } } };
            s.drums.hatEvery = 1;
            s.drums.offbeatOpenHat = true;
            s.drums.snareLane = 3;   // clap
            s.drums.ghostChance = 6;
            s.drums.swingShift = 4;
            s.tracks[0].spec = { Role::bass, 16, -1, 36, 20, 4, 2, 30 };       // on every offbeat eighth
            s.tracks[1].spec = { Role::stab, 16, -1, 60, 30, 3, -1, 20 };      // three stabs, rotation by the seed
            s.tracks[1].poly = true;
            s.tracks[2].spec = { Role::melody, 32, 18, 48, 40 };
            v.push_back (s);
        }
        {
            Style s;   // four on the floor, a clap on 2 and 4, offbeat open hats, swung hats, deep minor ninths
            s.name = "house";
            s.label = "House";
            s.info = "A minor, 128 BPM: four on the floor, clap, offbeat open hats with swing, ninth chords on the offbeats.";
            s.bpm = 128;
            s.root = 9;
            s.scale = 2;
            s.progression = { { 0, { 0, 2, 4, 6, 8 } }, { 3, { 0, 2, 4, 6, 8 } }, { 5, { 0, 2, 4, 6 } }, { 4, { 0, 2, 4, 6 } } };
            s.drums.hatEvery = 2;
            s.drums.hatOffset = 1;   // the off sixteenths, so the swing moves them
            s.drums.offbeatOpenHat = true;
            s.drums.snareLane = 3;   // clap
            s.drums.swingShift = 12;
            s.tracks[0].spec = { Role::bass, 16, -1, 36, 20, 5, 3, 40 };       // syncopated
            s.tracks[1].spec = { Role::stab, 16, -1, 57, 30, 4, 2, 30 };       // chords on the offbeats
            s.tracks[1].poly = true;
            s.tracks[2].spec = { Role::bells, 32, 10, 48, 40 };
            v.push_back (s);
        }
        {
            Style s;   // the two-step: kick on 1 and on the "and" of 3, snare on 2 and 4, busy hats with ghosts
            s.name = "dnb";
            s.label = "Drum & bass";
            s.info = "E minor, 174 BPM: two-step beat with ghost snares, long sub bass, a slow pad and a melody.";
            s.bpm = 174;
            s.root = 4;
            s.scale = 2;
            s.progression = { { 0, { 0, 2, 4, 6, 8 } }, { 5, { 0, 2, 4, 6 } }, { 3, { 0, 2, 4, 6 } }, { 4, { 0, 2, 4, 6 } } };
            s.drums.kickSteps = { 0, 10 };
            s.drums.hatEvery = 2;
            s.drums.openHatChance = 10;
            s.drums.ghostChance = 12;
            s.tracks[0].spec = { Role::bass, 16, -1, 28, 20, 1, 10, 100 };     // long notes with the kicks (steps 1 and 11)
            s.tracks[1].spec = { Role::pad, 16, -1, 55, 30 };
            s.tracks[1].division = model::Division::quarter;                    // 16 quarters: four bars per chord
            s.tracks[1].poly = true;
            s.tracks[2].spec = { Role::melody, 16, 30, 52, 40 };
            s.tracks[2].division = model::Division::eighth;
            v.push_back (s);
        }
        for (auto& s : v)
            s.drums.salt = 10;
        return v;
    }();
    return all;
}

/** The style called `name`, or nullptr (ambient and the moods are not styles). */
inline const Style* findStyle (const std::string& name)
{
    for (const auto& s : styles())
        if (s.name == name)
            return &s;
    return nullptr;
}

inline Params styleParams (const Style& s, unsigned seed = 1)
{
    Params p;
    p.seed = seed;
    p.root = s.root;
    p.scale = s.scale;
    p.progression = s.progression;
    p.slow = false;
    return p;
}

/** The project's tempo (item 0x78, params 0x46-0x48 = BPM x 100 in three 7-bit parts, low first; KSCON's ksp_setlist.py).
    0 when the project has no such values or they read as an erased block (127 127 3). */
inline double tempo (ProjectData& p)
{
    int v[3] {};
    for (int k = 0; k < 3; ++k)
    {
        const int* c = cell (p, item::project, static_cast<std::uint8_t> (0x46 + k), { 1 });
        if (c == nullptr || *c == kUnreadable || *c < 0 || *c > 127)
            return 0.0;
        v[k] = *c;
    }
    const int bpm100 = v[0] | v[1] << 7 | v[2] << 14;
    return bpm100 >= 3000 && bpm100 <= 30000 ? bpm100 / 100.0 : 0.0;
}

/** Sets the project's tempo (30-300 BPM). False when the project has no such values. */
inline bool setTempo (ProjectData& p, double bpm)
{
    int* c[3];
    for (int k = 0; k < 3; ++k)
        if ((c[k] = cell (p, item::project, static_cast<std::uint8_t> (0x46 + k), { 1 })) == nullptr)
            return false;
    const int bpm100 = static_cast<int> (std::lround (std::clamp (bpm, 30.0, 300.0) * 100.0));
    for (int k = 0; k < 3; ++k)
        *c[k] = (bpm100 >> (7 * k)) & 0x7F;
    return true;
}

/** Writes patterns 1..variations of tracks first..last: track 1 goes to drum mode and gets the groove (the last variation
    with a fill), tracks 2-4 the style's roles with their time division, and the style's tempo. Returns an error (the project may be partly
    changed) or an empty string. */
inline std::string applyStyle (ProjectData& project, const Style& s, const Params& p, int firstTrack = 1, int lastTrack = 4)
{
    if (p.variations < 1 || p.variations > 16)
        return "variations must be 1-16";
    for (int t = firstTrack; t <= lastTrack; ++t)
    {
        if (t == 1 && ! model::setDrumMode (project, true))
            return "this project has no drum mode value for track 1";
        for (int v = 0; v < p.variations; ++v)
        {
            std::string e;
            if (t == 1)
            {
                auto spec = s.drums;
                spec.fill = p.variations > 1 && v == p.variations - 1;
                e = model::writeDrumPattern (project, v + 1, makeDrumPattern (p, spec, v));
            }
            else
            {
                const auto& ts = s.tracks[t - 2];
                const auto base = model::readPattern (project, t, v + 1);
                auto pt = makeRolePattern (p, ts.spec, v, base.defaults);
                pt.config = base.config;
                model::setDivision (pt, ts.division);
                model::setPoly (pt, ts.poly);
                e = model::writePattern (project, t, v + 1, pt);
            }
            if (! e.empty())
                return "track " + std::to_string (t) + " pattern " + std::to_string (v + 1) + ": " + e;
        }
    }
    if (! setTempo (project, s.bpm))
        return "this project has no tempo value";
    if (int* state = cell (project, item::project, 37, { 1 }))
        *state = 3;
    return {};
}

} // namespace ksp::gen
