# Code model (all header-only, C++17 + std, no JUCE unless noted)

- `Source/Ksp/Protocol.h`: frames (identity, global GET, open session, bulk GET/SET, commit), parsers, guards
  (`isReadOnlyRequest`, `isWriteFor`), note and scale names (middle C = C3 = 60).
- `Source/Ksp/Backup.h`: `ProjectData` = the recall plan's blocks with values (0..127 or `kUnreadable`); `readProject`
  (callback-based, tests use a fake device); `summary`; JSON in KSCON's `keystep-pro-backup` v1 format.
- `Source/Ksp/Restore.h`: JSON reader (`fromJson`: backup or single project), `writePlan` (all values, states last;
  unreadable values and chords with a note below the root are not sent; empty drum slots as 127), `compareReadBack`.
- `Source/Model/Pattern.h`: one sequencer pattern (length 1-64, root, scale, steps with up to 16 notes, 192 notes per
  pattern): `readPattern`, `writePattern`, `check`. Drum lanes and arpeggio are not touched. Time shift is stored 0-99
  (49 = on the step, -49%..+50%) and randomness 0-100 (% chance to play, 100 = always); `readPattern` clamps values
  beyond that (earlier versions let the knobs write up to 127) and `check` refuses them.
- `Source/Model/Overdub.h`: real-time recording onto a `Pattern` from raw MIDI (clock + note-ons of one channel):
  nearest step by clock ticks modulo the length, velocity from the note, the rest from the pattern defaults; pitches
  already on the step are skipped (the KeyStep playing its stored notes back); counts notes that did not fit.
- `Source/Model/NoteMove.h`: `moveNotes` moves / copies a group of notes by steps and semitones, all or nothing (range, 16 notes per step; same pitch replaced); `sameSteps` compares two patterns' steps (the grid's Undo). `tests/notemove_test.cpp`.
- `Source/Ksp/MidiLink.h` (JUCE): the USB port pair. `Source/Ksp/Device.h` (JUCE): `open`, `readSlot`, `backupSlot`,
  `writeSlot`, `setListener` (non-SysEx MIDI: notes, clock, Start/Stop, on the MIDI thread); used by `KspRestore` and the app.
- `Source/Model/DrumPattern.h`: one drum pattern of track 1 (24 lanes, up to 192 hits, length per lane, gate / velocity /
  time shift / randomness / pass mask per hit): `readDrumPattern`, `writeDrumPattern`, `check`, `isDrumMode`, `setDrumMode`.
  Ported from KSCON (layout in the file header); the lane masks are rebuilt and freed slots emptied (127). Drum swing
  and flags are left as they are. NOT yet verified on hardware: do a slot 10-16 round trip (backup first) before relying on it.
- Tests (`tests/`): `protocol_test`, `backup_test`, `restore_test`, `pattern_test`, `drum_test`, `overdub_test`; all with a fake device or none.
  CI (GitHub Actions, Windows only since 2026-10-29) builds everything and runs them.
- `Source/Gen/Theory.h` + `Roles.h`: the role engine. A `RoleSpec` (role drone / pad / melody / bells / bass, length, density, register,
  RNG salt) goes through `makeRolePattern`; `euclid` and `makeDrumPattern(DrumSpec)` build drum grooves (kick, backbeat snare, hats,
  ghosts, swing) for `Model/DrumPattern.h`. Ambient is the first preset (`ambientRole`); `gen_test` freezes its output with a hash.
  Presets for other styles and the window are still to do.
- `Source/Gen/Ambient.h`: experimental ambient generator. Four tracks (drone, pad, slow melody, bells) with pattern
  lengths 16, 13, 17, 19 so they drift apart, four variations over a I-VI-IV-V movement; deterministic per seed.
  `makePattern`, `apply` (into a ProjectData), `preview`. Test: `gen_test`.
- `Source/Model/ProjectMap.h`: the whole project as one value for the Project Map window (`buildMap`: 16 scenes and 4x16 patterns), plus `noteStats`, `playOrder`, `scenesUsing`, `loopSteps`. `tests/projectmap_test.cpp`.
- `Source/Model/Scene.h`: one scene (which pattern each track plays): `readScene`, `writeScene`, `check`. Layout decoded on
  the device: item 0x79, param 0x26 state (2 default, 3 saved), param 0x4C set to 3 (bitfield: current track and chain state, per KSCON's mcc-params.md), param 0x53
  [scene, track] = pattern, 0-based. Params 77-82 and the 16-value lists of param 84 are not touched (chains not investigated).
  `gen::applyScenes` makes 8 scenes: 1-4 one variation on every track, 5-8 melody and bells one variation on. Test: `scene_test`.
- Pattern playback settings (`Pattern::config`, item 0x7B..0x7E param 0x63, one per pattern, what SHIFT + track shows):
  bits 3-4 time division (0 = 1/4, 1 = 1/8, 2 = 1/16), bit 2 Poly, bit 6 Walk; base value 20 = 1/16, Fwd, Poly. Rand, 1/32, triplet
  and bits 0-1, 5, 7 not decoded (kept as the device has them). `setDivision`, `setPoly`. The generator sets drone, pad and bells to
  1/4, melody to 1/8 (bells at 1/8 were too busy for Leo), pad Poly (`Params::slow`, `KspGen --no-slow` keeps the device's values).
- Scene pattern chains (`Scene::chain`): item 0x79 param 0x54, [scene, track, 1..16] = patterns 0-based, 127 = unused; the track's
  param 0x53 value gets 32 added when the track has a chain (one observation: bit 5 = chain active, not confirmed). A chain made
  with the Chain button and not saved inside a scene is live state and is lost at power off. `Params::chains` / `KspGen --chains`
  chain every variation on every track of every scene. Param 0x4C is now written as 0 (it holds the current track).
- Moods (`gen::moodParams(name)`, `Params::progression`): each variation plays one extended chord (`Chord`: root degree + scale-degree
  offsets; the pad plays all of them, the melody leans on the chord tones, the bells take chord tones). `ethereal` C major
  (Cmaj7sus4, G7sus4, Am9, Fmaj9), `dark` D minor (Dm9, Bbmaj7, Gm7, Asus4+7), `meditative` G mixolydian (sus2 / add9);
  `classic` is the original D Dorian triads. Notes above 108 are folded an octave down.
- `writePlan (p, &before)`: with the slot as just read, only the blocks that differ from it are sent, plus every state block (a commit
  flips the valid / default mark of the states not sent). `compareReadBack` ignores the note values of a sequencer slot that is empty on
  both sides (step = 127): the device keeps its own defaults there (7, 100, 49, 100). `model::transpose` / `transposeTrack` move notes
  by semitones, range-checked.
- `Source/Host/PluginList.h` (JUCE): the VST3 list (`KnownPluginList`), the out-of-process `ProcessScanner`, the hidden set, `instruments()` / `effects()`. Phase 3 step 1; instances, audio and routing come next.
- `Source/Studio/StudioProject.h`: one library project: the KeyStep project (`ksp`, project number a placeholder), per track an instrument
  and a chain of effects (plugin description and state as opaque text, volume, mute, solo), the generator settings, name, notes, dates and
  a 32-hex id; round-trips exactly (`toJson` / `fromJson`, UTF-8 and control characters escaped). `Source/Studio/Library.h`: the folder
  (`<id>.kspstudio`, atomic writes, `index.json` rebuilt from the files when missing or out of date, bad files skipped and reported),
  `list` / `load` / `save` / `duplicate` / `remove`. Test: `studio_test`.
- `Source/Host/TrackInstrument.h` (JUCE): one hosted VST3 instrument (create, description and state as text, `process` for the audio
  thread). `Source/Host/Engine.h` (JUCE): four instruments mixed to the audio device; MIDI through two lock-free FIFOs routed by channel;
  an instrument is swapped with an enabled / busy pair of flags.
- `Source/Mix/Limiter.h`: the 3 ms look-ahead master limiter (ceiling -1 dBFS, final clamp). The engine sums the tracks post-fader, runs the
  master chain, then the limiter. Test: `mix_test`. (The six built-in send effects, `Source/Fx` and the Mix window were removed on 2026-10-30;
  older project files with `mix` or `fx.sends` still open, those keys are ignored.)
- `Source/Host/TrackEffect.h` (JUCE): one hosted VST3 effect (stereo in place, no MIDI, state as text). The engine has 20 effect
  positions (4 inserts per track, 4 master), each with the enabled / busy handshake of the instruments. `StudioProject`: `tracks[t].effects`
  (inserts, an empty name = empty position), `masterFx`.
- `Source/Mix/Transport.h`: the KeyStep as master clock: Start / Continue / Stop and the 24-per-quarter clock give playing, position in ticks
  (bar.beat.16th in 4/4) and tempo. `Source/Host/Recorder.h` (JUCE): WAV recording of the mix and the stems through `ThreadedWriter` (lock-free FIFO
  to a background thread), with a gate that the KeyStep's Start opens and its Stop closes after the tail. The engine feeds them from the MIDI thread
  (`midiFromDevice`) and the audio thread (`write` per tap, `advance` per block). Tests: `mix_test` (transport), `KspHostCheck --record`.
