# KeyStepPro Studio manual

> Looking for how to *use* the app? Read the [User guide](user/README.md). This folder holds the developer notes.

Context for future work on the project: what exists, how to run it, what was verified on the real KeyStep Pro, and
which rules protect the device. Short on purpose; the code and `CLAUDE.md` have the rest. Keep this folder current:
every PR that changes behaviour, tools or results updates the matching file (and the status table below).

1. [Protocol and device rules](01-protocol-and-safety.md)
2. [Command-line tools](02-tools.md)
3. [Code model (Backup, Restore, Pattern)](03-model.md)
4. [The Studio app](04-studio-app.md)
5. [Hardware log: what was verified and when](05-hardware-log.md)
6. [What can be shown live between the app and the KeyStep](07-live-sync.md)

Protocol details (frames, quirks) live in [`../protocolo.md`](../protocolo.md), in Spanish.

## Status (2026-10-30)

| Phase | What | State |
| --- | --- | --- |
| 0 | KspProbe: read-only connection test | done, verified |
| 1a | KspBackup: all projects and globals to JSON | done, verified (16 projects, 9.6 min) |
| 1b | KspRestore: one project to a slot, backup first, read-back | done, verified (project 1 to slot 16) |
| 2 | Pattern model + Studio app (edit notes, overdub from the KeyStep, write to slots 10-16) | model + app done; edit and write to slot 15 verified by read-back (device display not yet checked); overdub built, not yet tried with the KeyStep |
| 3 | VSTi host per track: one VST3 instrument per track, inserts and master chain, KeyStep as clock, WAV recording | done, used live |
| 4 | Generator: ambient (moods) and style presets techno / house / drum & bass (drums on track 1) | in progress |
