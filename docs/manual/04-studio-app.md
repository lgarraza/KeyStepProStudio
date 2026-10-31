# The Studio app (`App/Main.cpp`, target `KeyStepProStudio`)

Pattern editor, first version.

Icon: the white chassis, black display strip and the four track colours, drawn by `Tools/make_icon.py`
(`App/Resources/`: `icon.png` / `icon_small.png` go to JUCE's ICON_BIG / ICON_SMALL, which builds the exe's .ico;
`icon.ico` 16-256 px for shortcuts). Re-run the script if the palette in `App/KspStyle.h` changes.

- **Open backup...** loads a backup file (or pass the path on the command line); the project with content is picked.
  **Read from KeyStep** reads the chosen project from the device instead.
- Track, pattern (a `*` marks patterns with data), length, root and scale; the grid shows two octaves (`-`/`+` move
  by an octave): right click on a note deletes it; left click on an empty cell adds a note (up to 16 per step; new notes take the
  pattern's default gate and velocity) and selects it (the track's instrument, if loaded, plays it for 0.3 s), on a note just selects it (red outline); dragging a knob edits it. Steps run left to right.
- Moving notes: drag a note to another step and / or row (snap to cells, never to a row that is not shown); it is heard when its pitch changes. Ctrl / Shift + click adds or removes a note from the selection, Shift + drag on an empty cell selects the notes inside a rectangle, and dragging any selected note moves the whole group by the same steps and semitones. Alt + drag copies. A note landing on the same pitch of a step replaces the one there; a move that would leave the 64 steps or pass 16 notes in a step is not applied (the group stays where it last fitted). Delete / Backspace removes the selection; Ctrl+Z (grid focused) undoes the last add, delete or move (30 deep; it is dropped if the pattern was changed another way in between).
- Under the grid, five KeyStep-style knobs (Pitch, Gate, Velocity, Time Shift, Randomness; red LED ring) edit the selected note: drag up / down
  (Shift = fine) or use the wheel. They are dark until a note is selected. Time Shift goes from -49% to +50% of a step (the ring
  lights from the centre); Randomness is the chance the note plays, 0-100% (100% = always, lower = it skips some passes).
- **Copy to** (track + pattern, top right): copies the pattern shown, with its unsaved edits, over another pattern of
  the project; it reaches the KeyStep only with **Write to slot**.
- **Track -8va / Track +8va** (top row): move every pattern of the current track one octave in the project (refused if a note
  would leave 0-127); it reaches the KeyStep with **Write to slot**.
- **Send to slot N** (single button above the track cards, replaces Read from KeyStep / Write to slot / Slot box): shows "Read slot N from KeyStep" when the project matches the KeyStep and "Send to slot N" (blinking) when it has unsent edits; N is the slot the project came from, any of 1-16. Confirms, backs the slot up, writes, reads back and reports "written and
  verified" or the differing values. Edits are kept in memory when you switch pattern or track.
- **Rec** (overdub, bottom row): with the KeyStep connected, press Rec, then Play on the KeyStep and play over the
  loop; notes on the chosen channel (default = track number) are added to the pattern shown at the nearest step of the
  chosen size (1/4 .. 1/32, default 1/16; it must match the track's time division on the KeyStep). Velocity comes from
  the key, gate from the pattern default (not mapped yet). Notes already on that step are skipped, so the sequencer's
  own playback is not recorded twice. **Stop rec** ends the take; **Undo take** removes it. Switching pattern, track or
  project, reading or writing stops recording. The KeyStep keeps playing the stored pattern until you write the slot,
  and pattern chains are not followed (the position is counted from Start, modulo this pattern's length).
- **Drum grid**: when track 1 is in drum mode (the KeyStep's drum track, or after a generated style) the grid becomes 24 lanes x 64 steps
  (`App/DrumGrid.h`, lane 1 = kick at the bottom, names and notes of the default GM drum map). Click a cell to add / remove a hit, drag
  to paint more of the same, mouse wheel on a hit for its velocity (its shade), click a lane name to hear it, ruler or length box for the
  last step (lanes that followed the length follow it). Root, scale, octave, knobs, Rec and Copy are disabled there. Up to 192 hits.
- **Drums** (card 1, lit when on): switches track 1 between drum and sequencer mode in the project (both kinds of patterns stay
  stored; the grid follows). **Clear** (left of Copy to) empties the pattern shown, notes or drum hits; length and settings stay.
  Both are ordinary edits: they reach the KeyStep with Write.
- Not yet: per-note velocity/gate, scenes, project names, undo.
- Smoke test without a KeyStep: `xvfb-run` the binary with a backup file; the grid should show its notes.
- Write to slot now sends only what differs from the slot as it was just read (plus all state blocks), about 75 s in total instead of
  about 200 s (backup read, write, commit, full read-back).
- **Layout**: the top bar keeps the KeyStep's own actions (Library, Save, Save as, Open backup, Read from KeyStep, project box, STEPS / BPM) plus Generate..., Project Map and FX... (also in Tools); the extras live in the **Tools** menu at its right: Scan plugins, Plugins..., MIDI monitor, Track -8va / +8va, Rec WAV (with mode, tail, Follow the KeyStep and the recordings folder), Audio..., FX... and Test note. Below the grid stay the T1-T4 instrument rows, the transport / REC status and the meter. Wherever these notes say "button" for those items, read "Tools menu item".
- **Scan plugins**: scans the VST3 folders one file at a time in `KspVstHelper.exe` (next to the app; a plugin that crashes or hangs only
  kills that child and is blacklisted), keeps the list in `Documents/KeyStepPro Studio/KnownPlugins.xml`. First run on Leo's machine:
  116 instruments and 173 effects (289 types, a few minutes; Windows asked once to allow the unsigned helper).
- **Plugins...**: the list with filter box, Instruments / Effects selector and a **Hide** checkbox per plugin (kept in `HiddenPlugins.xml`,
  not deleted; **Show hidden** brings them back into view). `PluginList::instruments()` / `effects()`, what the tracks will offer,
  leave the hidden ones out.
- **Library** (the truth of the app; the KeyStep only holds a volatile working copy): `Documents/KeyStepPro Studio/Library/`, one
  `<id>.kspstudio` file per project plus `index.json`. **Library...** lists the projects (name, change date, what the KeyStep project
  holds, instruments) with Open (or double click), Duplicate and Delete; **Save** puts the project shown into the library (asks a name the
  first time: a project opened from a backup or read from the KeyStep is always new to the library) and then overwrites it;
  **Save as...** asks a name and keeps it as a new library project. At start the app opens the most recent library project, with no KeyStep connected. **Open backup...** and **Read from
  KeyStep** bring a project in, **Save** keeps it. **Write to slot** loads it into any of the 16 slots (the slot is backed up and read
  back; the warning says the KeyStep is only a working copy). Not yet: the instruments and effects of the four tracks (the file format
  has room for them), naming a project from the window, loading several projects in one go.
  **Closing the app** asks Save / Discard / Cancel when the project has changes not saved to the library (Save reuses the name prompt), then, if the changes were never written to the KeyStep, a notice with OK / Cancel so you can go back and write them.
- **Instruments** (two rows under the grid, T1 to T4): one hosted VST3 instrument per track of the KeyStep, chosen from the scanned list
  (hidden plugins left out), with **Edit** (the plugin's own window), **M** (mute) and a volume slider. The notes of the KeyStep reach
  each instrument on the track's MIDI channel (T1 to T4 = channels 1 to 4, the KeyStep's defaults and Leo's settings); the app connects
  to the KeyStep's MIDI by itself when an instrument is loaded. **Audio...** picks the output device, **Test note** plays a C3 on every
  loaded instrument (no KeyStep needed) and the label shows the output peak. **Save** keeps the instruments, their states, volume and
  mute in the library project; opening it loads them again. A plugin that is missing on the machine shows as "Missing: name" and is
  kept untouched in the file. Not yet: effects per track, the track's MIDI channel setting, transport and recording, a master limiter
  (there is only a safety clip).
- The label under the instruments shows the output peak, how much the master **limiter** is pulling down, and the **BPM** read from the
  KeyStep's MIDI clock (120 until it plays). (A Mix window with six built-in send effects and a master boost existed until 2026-10-30; it was
  removed: effects are VST3 inserts per track and the master chain.)
- **FX...** (next to Test note): the effect positions, each with a box of Leo's VST3 effects (hidden plugins left out), **Edit** (the plugin's own window)
  and **Byp** (bypass): four **inserts on each track** (after its instrument, before its fader) and a **master chain** of four (before the limiter). They are saved
  with the project (state, bypass); a plugin missing on the machine shows as "Missing: name" and is kept in the file. Plugin latency is not
  compensated yet.
- **The 64 steps**: the grid always shows all 64 steps with a numbered ruler on top. The part up to the last step is bright; the last step is
  marked with an orange **END n** flag and a line, and everything after it is dimmed (notes there are kept and shown dim: the KeyStep does not
  play them). Clicking or dragging on the ruler sets the last step (the same as choosing the length in the box). Vel and Gate bars show all 64.
- **Hover card**: moving the mouse over a step that has notes opens a card beside that step, with an **arrow pointing at the note** (the card goes to the
  right of the step, or to its left near the end of the grid), while the step's column is tinted and the note outlined in the grid. It shows: the note (name, MIDI number, frequency), the
  track, pattern and MIDI channel, the step number with its page of 16 and its beat, **velocity** and **gate** with bars, **time shift** and
  **randomness**, the four **passes** in which the step plays (lit when the pass mask has them), and every note of the step. Over a lane or
  an empty row of the step it describes its highest note; after the last step it says the KeyStep does not play it. Time shift is shown
  in % of a step and randomness in %; gate is the 0-127 value the KeyStep stores (its musical scale is not decoded yet).
- **Transport and WAV recording** (the row above the instruments). The KeyStep is the master: the label shows PLAY / STOP, the position (bar.beat.16th
  counted from its last Start, 4/4), the elapsed time and the tempo read from its MIDI clock. **Rec WAV** records the **mix** (after the limiter),
  the **stems** (T1 to T4: what each track sends to the mix, with its instrument, inserts and fader) or
  **both**, as 24-bit stereo WAV at the audio device's rate, to `Documents/KeyStepPro Studio/Recordings/<project>-<date>-<mix|T1..T4>.wav`
  (**Folder** opens it). With **Follow the KeyStep** (default) it arms and waits: nothing is written until the KeyStep says Start / Continue; after
  its Stop it keeps recording for the **tail** (0 to 30 s, 5 by default: the reverbs and delays ring out) and then closes by itself. Without it,
  it records from the click to the next click. An armed recording that never got audio leaves no files. The stems add up to the mix before the
  master chain and the limiter, not after them.
- **MIDI monitor** (button at the top): shows what the KeyStep sends while its controls are used. A table of the **controls seen** (one row per
  distinct control: notes of a channel with their range, each CC of each channel with its name, pitch bend, channel and key pressure, program
  change, Start / Stop / Continue), each with its channel, last value, range and count, above a **log** of the last messages (time, channel, what).
  The clock is only counted (24 a quarter note would drown the log). **Pause**, **Clear** and **Copy all** (the table and the log to the clipboard).
  It opens the KeyStep's MIDI port if the app has not. Its purpose is to find out what each knob, strip, pad and button really sends, to design
  how they map to the app (docs/manual/07-live-sync.md lists what is still to check).
- **Project Map** (top row): the open project as one picture, in its own window (it refreshes when you bring it to the front; the scene matrix edits: click a cell to pick that track's pattern for the scene, which drops its chain, right-click a scene number for Copy / Paste / Clear, "Undo edit" takes back the last one; `Scene.h` `clearScene`; chain editing is not done). Three views: the
  **scene matrix** (scenes 1-16 by tracks T1-T4, each cell the piano roll of the pattern that track plays, its number, "x n" (chain length, not the MIDI channel) when it plays a chain; hatched when
  there is nothing to play), the **chain timeline** of one scene (its patterns to scale in steps, so different lengths show how the loops drift) and the **pattern
  bank** (16 patterns per track and the scenes that use each). Hover or click a cell for the pattern, length, notes, range, scale and chain on the right. Drums
  are not shown yet (the model has no drum patterns).
  A strip under the tabs summarises the project (scenes saved, patterns recorded, and per track its patterns, chains and pattern lengths); in the matrix the
  hovered scene and track are highlighted and a chain shows as a strip along the bottom of the cell, one segment per pattern, wide as its length.
  Screenshots to add under `docs/manual/img/` once taken on Windows: `project-map-matrix.png`, `project-map-chains.png`, `project-map-bank.png`.
- **Generate...** (top row): the generator inside the app. Choose a **style** (Ambient, or the `Gen/Styles.h` families Techno, House, Drum & bass) and a
  **mood** (Ambient: Classic, Ethereal, Dark, Meditative; House: Deep, Chill, Melodic; one each for Techno and Drum & bass), a **seed** (New seed rolls a
  number; the same seed gives the same music), whether the scenes **chain** the variations and whether the time divisions are **slow**, and
  **Generate into this project**: patterns 1-4 of the four tracks and scenes 1-8 of the project that is open are replaced (drums, the arpeggiator
  and other patterns are left alone). A style instead puts track 1 in drum mode, writes drum patterns 1-4 there (default drum map, GM notes),
  patterns 1-4 of tracks 2-4 (bass, chords, lead, with their division and Mono/Poly) and scenes 1-4; and the project's tempo (techno 130, house 118-128, dnb 174 BPM). While
  track 1 is in drum mode its instrument listens on channel 10 (the KeyStep's default Drum output). Then play it through the instruments with the KeyStep, hear it, edit it, **Save** it to the library and load it into
  a slot with **Write to slot**. **Undo** brings the project back as it was before the last generation. The settings used are saved with the project
  (the library file keeps the style, the mood and the seed). It needs a project open (a library project, a backup or one read from the KeyStep).
