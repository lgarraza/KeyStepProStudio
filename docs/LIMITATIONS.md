# Limitations and attempts

KeyStepPro Studio is a hobby project built on Arturia's undocumented SysEx protocol. This page is an honest account of what
we tried, what did not work out, and where the app's real limits are, so you can decide whether it fits your setup. In some
cases a simpler workflow (the KeyStep Pro on its own, or MIDI Control Center) will serve you better than this app.

## What the app is good at

- **Editing on a big screen.** Patterns, scenes and chains are much easier to see and change on a monitor.
- **Library and backups.** Projects live on your computer, can be edited without the KeyStep connected, and every write is
  preceded by an automatic backup and followed by a read-back check.
- **Generator.** Writes patterns, scenes and chains in a chosen style, with undo.

## The core constraint: the app reads flash, not the live state

The SysEx protocol exposes what is **saved in the KeyStep's flash**, not what it is playing right now. The app is therefore a
*librarian*: it reads a project, you edit it, and it writes it back. Live, the app only sees what arrives as normal MIDI
(notes, clock, Start/Stop/Continue, Program Change). Almost every limit below follows from this.

## Attempt 1: live editing over USB (parked)

Idea: edit a pattern in the app and hear the change immediately, without writing to the KeyStep.

What we tried: sending notes to the KeyStep's USB MIDI input while editing.

- **Works:** auditioning a note when you add a step. The KeyStep forwards it to the computer, so a DAW or the Full edition
  can play it.
- **Does not work well:**
  - If the app plays the edited pattern while the KeyStep's transport runs, notes **double** with the version still saved
    in the KeyStep.
  - **Deleted steps keep sounding** (from the KeyStep) until you write the project.
  - The KeyStep's **LEDs cannot be driven from outside**; the hardware never shows the edit.
  - Drum notes sent on channel 10 **flash the 16 step LEDs**.
  - The app only sees clock and transport if it **holds the USB MIDI port**. On Windows the port is normally exclusive, so
    sharing it with a DAW or MIDI Control Center is a risk.
  - The KeyStep's **Sync output settings** can interfere with transport messages.

Status: the experiment branch (`claude/project-thread-ruyppr`) is parked and was never merged. The supported workflow stays
edit, write, play on the KeyStep.

## Idea 2: the app as its own transport, with MIDI out to a DAW (not started)

This would remove the doubling problem, but:

- It needs a **virtual MIDI port**. JUCE can create one on macOS only; on Windows you need a third-party driver such as
  loopMIDI.
- It means **re-implementing the KeyStep's playback** (passes, gates, swing, chains, scene logic) so that it sounds the same.
  That is a large project and would drift from the hardware over time.

## KeyStep 37 MK2 and other Arturia devices

Not feasible as a port. Arturia has said its SysEx is a private protocol and has not published a specification. The MK2 also
has a single sequencer, with no projects, scenes, chains or drum track, so most of the app's model has nothing to map to.
The app checks the device identity and firmware before writing, which also protects against connecting the wrong
hardware. Supporting the MK2 would be a separate effort: capture MIDI Control Center traffic, map the messages, and test on
real MK2 hardware.

## Other known limits

- **Firmware.** Only the official KeyStep Pro firmware **2.5.20** is tested. On any other version the app reads but refuses
  to write.
- **Drums in scenes** are not fully decoded. Drum patterns are written and verified; drum lanes inside scenes are not.
- **Cautious areas.** The app writes to the 16 slots (always backed up and read back). Global settings are handled with
  extra care, and writing to slots you care about is best done after a full backup.
- **Reads are slow.** A full backup of 16 projects takes about 10 minutes (thousands of small SysEx requests).
- **Windows port exclusivity.** Close MIDI Control Center and any DAW using the KeyStep before starting the app.
- **Unsigned binaries.** Windows builds are not code-signed (a SignPath Foundation application was not accepted). Defender
  flagged one installer once as a machine-learning false positive (`Trojan:Win32/Wacatac.C!ml`); releases ship SHA-256
  checksums and VirusTotal links, and the code is open for inspection.
- **macOS builds** are ad-hoc signed and not notarized (no Apple Developer account): allow the app once in System Settings >
  Privacy & Security > Open Anyway.
- **VST3 only** in the Full edition (no AU, no VST2), and no plugin latency compensation.

## If you want to build on this

The project is open source (AGPL-3.0) and forks are welcome. If it is useful to you, great; if you need something it does
not do, please change it.

- **Protocol layer** (header-only, no JUCE, with tests): `Source/Ksp/Protocol.h` (frames and parsing), `Backup.h`,
  `Restore.h`, plus the generated `RecallPlan.h` and `Globals.h`. Notes and pitfalls: [protocolo.md](protocolo.md). The
  protocol itself was mapped by [KSCON](https://github.com/canadianblaken/keystep-con).
- **Models:** `Source/Model/` (patterns, drums, scenes, project map) and `Source/Studio/` (library).
- **Tests without JUCE or hardware:** see "Build from source" in the [README](../README.md).
- **Parked work:** the live-edit branch above is a starting point for anyone who wants to try a different approach.
