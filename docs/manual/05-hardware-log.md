# Hardware log (Leo's KeyStep Pro, firmware 2.5.20 build 2085)

- 2026-10-29, phase 0: identity, 15 globals and the notes of saved patterns match what Leo recorded (project 1,
  2 patterns with 3 notes in the first 16 steps). The first probe showed nothing because the project was not saved
  (reads come from flash).
- 2026-10-29, phase 1a: full backup of 16 projects + 129 globals in 9.6 min, no errors. Only project 1 has content
  (T1 2 patterns, T4 2 patterns, 0 scenes); 2 values of 117,783 were unreadable (>= 0xF7).
- 2026-10-29, phase 1b: project 1 restored to the empty slot 16: 8949 bulk SETs in 116 s, every value read back as
  written, and Leo confirmed on the device that project 16 matches project 1. The 2 unreadable values were not sent.
- Leo's KeyStep holds nothing that needs keeping (his words, 2026-10-29); standing permission to write tests to
  slots 10-16 with backup and read-back. Slots 1-9 and globals need an explicit OK.
- Process notes: CI is ~1 min on macOS and ~4 min on Windows. A JSON reader that scanned the rest of the file per
  number was quadratic on 10 MB backups (fixed in PR #4).
- 2026-10-28, phase 2: Studio app opened the full backup, one note added to project 1 / track 1 / pattern 1 and written to
  slot 15 from the window: backup of the slot, write, read-back, "Slot 15 written and verified" (about 4 min total).
  Not yet checked by ear/eye on the device itself.
- Tool note: launching the app with the computer-use open_application tool starts a second, empty instance; launch it from the shell with the backup path instead.
- 2026-10-29, generator experiment: `KspGen --to 16 --seed 1 --write` (D Dorian) wrote patterns 1-4 of all 4 tracks to
  slot 16, 8949 bulk SETs, every value read back as written. The slot before is in Backups (ksp-pre-restore-P16-2026-10-29_120001).
  Not yet listened to on the instrument (open question: how gate 100-120, shift and random sound, and track 1 mode).
- 2026-10-29, scene layout (read-only diffs on project 16, SysEx reads only the saved project): the first diff also showed
  edits made earlier on the device and never saved (a note, a gate), which came out with the first save: always save once
  and take a fresh base before a diff. Scene 1 (T1 pattern 2, T2 3, T3 4, T4 1): item 0x79 p0x26 2 -> 3, p0x4C 0 -> 3,
  p0x53 [1, t] = 1, 2, 3, 0. Scene 2 (T1 1, T2 3, T3 1, T4 2): p0x26 -> 3, p0x4C -> 3 again, p0x53 [2, t] = 0, 2, 0, 1.
  Also 0x78 p75 went 0 -> 1 when the second scene was saved (maybe the selected scene; unconfirmed). Generated scenes
  written to slot 16 and read back identical (8 scenes). Not yet checked on the device by Leo that they recall as expected.
- 2026-10-29, per-pattern playback settings (SHIFT + track): after a clean base, one change on each track saved on the device
  (T1 division 1/4, T2 direction Walk, T3 Mono, T4 division 1/8) changed exactly one value each, item 0x7B..0x7E param 0x63 of the
  pattern selected at that moment: 20 -> 4, 84, 16, 12. It is per pattern, not per track. 0x78 p75 was 6 when scene 7 was the one
  selected on the device (so probably the selected scene, 0-based). Slow generator version (drone/pad 1/4, melody/bells 1/8)
  written to slot 16 and read back identical; not yet listened to.
- 2026-10-29, listening to the generated project: drone and pad at 1/4 sound right, bells at 1/8 were too busy (now 1/4). Pattern lengths of
  13/17/19 steps are fine: besides the 16/32/48/64 buttons the Last Step button takes any length, and the offset between tracks works.
- 2026-10-29, pattern chain (read-only diff, clean base, project 16): T1 chain 1-2-3-4 and T2 chain 4-3-2-1 made on the device and
  saved: **0 of 8951 values changed**. On the device the chains survive switching to project 15 and back (Leo), so they are not
  lost, but they are not in the saved values the SysEx read sees. KSCON's mcc-params.md (read afterwards) says 0x78 p75 =
  current scene (as guessed), 0x79 p76 = bitfield of the scene's current track and chain state, 0x79 p84 = "Scene / Track /
  Chain" (16 values per scene and track): the expected place for a chain, and it did not change. Open: is the chain only in
  the device's RAM (a power cycle would tell) or stored somewhere outside the recall plan?
- 2026-10-29, pattern chain, closed: after a power cycle the chains of project 16 are gone (Leo). They are live performance state,
  kept in the device's RAM while it is on, not saved in the project, so SysEx can neither read nor write them. Do not generate
  pattern chains; use scenes (saved) for song structure. Scene chains were not tested.
- 2026-10-29, pattern chain saved in a scene (the way that works, found by Leo; clean base, read-only diff): chain 1-2-3-4 on T1 of
  scene 1 changed item 0x79 p84 [1,1,1..4] from 127 to 0 1 2 3, p83 [1,1] 0 -> 32, p76 [1] 3 -> 0 (current track 4 -> 1), and the
  patterns of T2-T4 in scene 1 to 3 (Leo's navigation). Generated version with chains in every scene written to slot 16 and read
  back identical; not yet listened to (open: does the 32 flag matter, do the chains advance on their own on the device).
- 2026-10-29, generated chains (slot 16, written by KspGen --chains): Leo sees the chain active on the device after recalling a scene, so
  the chain list (p84) plus 32 on p83 is enough. Not yet reported: whether the patterns advance on their own.
- 2026-10-29, moods written with `KspGen --seed 1 --chains --mood <m> --write`: ethereal to slot 15, dark to slot 14, meditative to slot 13
  (backups of each slot in Backups, read-backs in the run log). Slot 16 keeps the classic version. Not yet listened to.
- 2026-10-29: slots 14 and 13 first read back with 1 differing block: the project state (item 0x78 param 37) written as 2 came back 3 on
  slots that held no project before (the commit makes the project valid). `gen::apply` now writes 3; both slots rewritten and verified.
  Found with the new KspCompare.
- 2026-10-29, register: Leo had to go to octave -1 on the KSP to see the whole pad chord of project 15, and the pad notes reached the host
  exactly 12 semitones below what was written (a pad solo clip: F1, G1, A1, D2, E2 for notes written at 53, 55, 57, 62, 64). Likely the track's
  octave button at -1 (not confirmed: the saved octave, 0x7A p86, reads 2). The generator now puts the pad at 60-83 (one octave higher than before; the drone stays at 36, Leo asked to go back); slots 15, 14 and 13 rewritten and verified. The classic slot 16 still has the old registers.
- 2026-10-29, diff write: slot 15 rewritten with only the changed blocks (138-193 bulk SETs, 77 s total with backup read and full read-back,
  against about 200 s). The first run read back 36 differing blocks, all note values of empty slots (device defaults 7, 100, 49, 100 instead of
  the 127 sent); compareReadBack now ignores those and the second run verified. Leo can also shift a track live on the KSP with SHIFT +
  Octave up/down to find the register he wants.
- 2026-10-29, phase 3 step 1 (VST3 scan): 289 plugin types found on Leo's machine (116 instruments, 173 effects) with the out-of-process
  scanner, no crash of the app. Hide checkbox tried (hide, show hidden, unhide).
- 2026-10-29, library: a backup opened in the app was saved to the library with a name, listed in Library..., and the app reopened it at start
  with no KeyStep connected (the test project was deleted afterwards). The KeyStep keeps no project name or date (official firmware), so
  names and dates live in the library.
- 2026-10-29, phase 3 step 2 (instrument): KspHostCheck on Leo's plugins: Mini V4 (Arturia), Diva and Pigments load in 0.1 to 0.8 s, sound
  and restore their state; Omnisphere and Kontakt 8 start without a patch (silent until a state is restored); HALion Sonic crashes while
  rendering in the windowless check (Leo: its installation has problems, ignore it). In the app: Mini V4 on track 1 sounded through the
  audio device (-12.7 dB peak with the test note), its editor opened, and a project saved with it came back with Mini V4 loaded and sounding.
  Arturia instruments are the reference for tests. The MIDI path from the KeyStep (connect, route by channel) is built; listening to the
  KeyStep playing the instrument is the next check.
- 2026-10-29, the KeyStep Pro playing the Studio: with project 16 and Mini V4 on track 1, the KeyStep's sequencer plays the hosted Mini V
  through the audio device; Leo confirmed it sounds right ("suena Mini V y hermoso"). MIDI in (USB), routing by channel, audio out and
  the project round trip are verified on the real KeyStep.
- 2026-10-29, mix: Mini V4 (Arturia) played through each of the six sends and the +9 dB master limiter (KspHostCheck --mix): every send leaves a tail
  (at 3 s: Plank Loop 0.36, Nostalgia for Infinity 0.38, Airlock 0.08, the rest under 0.02) and the peak stays at the ceiling (0.891). In the app
  the Mix window opened with its knobs working; always-on-top for it made the window disappear and was dropped. Not yet listened to by Leo.
- 2026-10-29, VST effects: KspHostCheck --effect on Arturia's Rev PLATE-140, Delay TAPE-201, Comp VCA-65 and Efx AMBIENT: each processes audio (the output
  differs from the input) and restores its state. The FX window was built and compiles; not tried by hand (the desktop automation lost the focus to
  Chrome), Leo to try: an effect on a send, an insert, the master chain.
- 2026-10-29, transport and recording: in the app, Rec WAV (mix + stems, not following the KeyStep) with Mini V4 on track 1 and the test notes wrote six 24-bit
  stereo WAVs at 44.1 kHz of 9.85 s; the T1 stem and the mix both peak at 0.2525 and the other stems are silent (no other instrument, no sends open).
  Armed with "Follow the KeyStep" the label read "armed: waiting for the KeyStep to play". Not yet tried: a recording started by the KeyStep's Play.
