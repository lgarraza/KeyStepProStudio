# What can be shown live between the app and the KeyStep Pro

The app is the truth and the KeyStep is a volatile working copy (CLAUDE.md). This page defines what the two can share *live* and what they cannot,
so the interface only shows what is real. Each line says how sure we are: **verified** (seen on Leo's KeyStep), **notes** (from KSCON's reverse
engineering, not tried here) or **to check** (needs the MIDI monitor, see the end).

## 1. Live, device to app (MIDI stream, nothing to ask for)

| Information | Source | State |
| --- | --- | --- |
| Notes played by each track (note, velocity, on / off), on the track's MIDI channel | MIDI notes | verified (the instruments play from it) |
| Tempo | MIDI clock (24 per quarter), measured | verified (78.9 BPM read from project 16) |
| Play / stop / continue | Start, Stop, Continue | verified |
| Position (bar.beat.16th) since the last Start | counting the clock ticks | verified (shown in the transport row) |
| **Playhead of each track** (which step is playing) | computed: ticks, the track's time division and last step, direction forward | derivable: needs the project in the app to match the device; not for Rand / Walk directions |
| Which notes are sounding right now (key flashes on the grid) | MIDI notes | derivable |
| The five knobs (Pitch, Gate, Velocity, Time Shift, Randomness) | MIDI CC; number, channel and range are global settings (default CC 74) | notes (Globals.h has the settings); to check what they send while editing steps |
| Mod strip, pitch strip, sustain, aftertouch of the keyboard | MIDI CC / pitch bend / pressure | to check |
| Project change, pattern change per track | Program Change when "PC Send" is on for the track | notes; to check |

## 2. Live, both ways, by SysEx (the global settings)

The 158 global settings (MIDI channels, knob CC assignments and ranges, brightness, fixed velocity, PC send...) are read and written **live**: a write
takes effect at once and reads back (KSCON live test: brightness 1 to 2 and back). The app can show them and edit them in real time. They are not
part of a project.

## 3. Saved state only (SysEx project sessions)

Patterns, scenes, tracks' settings, tempo, the selected scene (0x78 p75): a read session copies the project **from flash**. Anything edited on the
KeyStep and not saved is invisible to the app (verified: a chord and an octave edited live never reached the saved copy; unsaved notes appeared
only after saving). After a save on the device the app can notice a change by re-reading (the quick check samples ~30 % of the project), but it
cannot know that something was edited, nor which project the device has selected.

## 4. Not available at all

The display, the LEDs, which track is selected, mute / solo as shown, the pattern being edited, step-edit, hold, the arpeggiator state, the current
pattern of a track (unless PC Send reports it). The app cannot light the KeyStep's LEDs or write to its display with the official firmware.

## 5. App to device, live

- MIDI: notes and clock to the KeyStep's input (it follows external clock if set to), and the global settings of section 2.
- Everything else (patterns, scenes) goes through a write session and a commit: seconds to minutes and it reloads the project, so it is a *load*,
  not a live link.

## What the interface should show, then

1. A live **playhead** on each track's grid and the **notes sounding**, from the clock and the MIDI notes (section 1).
2. The **knobs** and strips as live meters when they are assigned (after the monitor confirms what they send).
3. A **device status chip** per slot: which library project was loaded there, when, and whether a sample of the slot still matches ("same as
   loaded" / "different: edited or replaced on the device, load again or import").
4. The **global settings** panel with live read / write (section 2).
5. Never a claim that the KeyStep "is in sync": only that it matched at the last read.

## To check with the MIDI monitor

What each knob, strip, pad and button sends (CC numbers, channels), whether Program Change reports pattern or scene changes (PC Send per
track), whether song position pointer is sent, and whether the step playhead matches the computed one with the swing and the time division of Leo's
projects.
