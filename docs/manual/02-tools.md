# Command-line tools

Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build --config Release`. Binaries in
`build\<Tool>_artefacts\Release\`. No-JUCE tests:
`cmake -S . -B build-tests -DKSP_TESTS_ONLY=ON && cmake --build build-tests && ctest --test-dir build-tests`.

| Tool | Writes? | Use |
| --- | --- | --- |
| `KspProbe` | no | Identity, 15 globals, patterns and notes of one project: `--project N`, `--scan` (valid patterns in all 16), `--list` (ports). Report: `ksp_probe_report.txt`. |
| `KspBackup` | no | `--projects 1-16`, `--no-globals`, `--out file`. Default: `Documents/KeyStepPro Studio/Backups/ksp-backup-<date>.json` (KSCON's format, rewritten after each project). ~36 s per project. |
| `KspRestore` | **yes** | `--file backup.json --project N [--to M] [--write] [--no-verify]`. Without `--write`: dry run (still saves the pre-restore backup of slot M). ~2 min per project. |

Example (done on 2026-10-29): `KspRestore --file ksp-backup-2026-10-28_222752.json --project 1 --to 16 --write`.

## KspGen (experiment: ambient patterns into a test slot)

`KspGen [--to 1-16] [--seed N] [--root 0-11] [--scale 1-5] [--tracks 1-4] [--variations 1-16] [--no-scenes] [--no-slow] [--chains] [--mood classic|ethereal|dark|meditative] [--style techno|house|house-chill|house-melodic|dnb [--musical]] [--write]`. Reads the slot and
saves it to Backups first, puts generated patterns 1..variations over the chosen tracks (the rest of the slot stays),
saves the project to `Documents/KeyStepPro Studio/Generated/`, and with `--write` sends it, commits and reads back.
Without `--write` it is a dry run. Any slot 1-16. Track 1 is written as a sequencer track, except with `--style`: then it goes
to drum mode and gets the style's drum patterns, and tracks 2-4 get bass, chords and lead (scenes 1..variations).

## KspCompare (no device, no JUCE)

`KspCompare written.json readback.json [--project N]`: compares a project as written (KspGen's `Generated/` file, a backup) with a read-back
(KspBackup output) using the same rules as the read-back check (`writePlan` fix-ups, then `compareReadBack`). Use it when a tool says
"value block(s) differ" to see which blocks.

## KspVstHelper (VST3 helper, run by the app)

`KspVstHelper <plugin file> <result.xml>`: loads one VST3 file, writes its plugin descriptions to the XML and exits 0; a crash kills only
this process. Not meant to be run by hand; the app starts it per file.

## KspHostCheck (no device, no window)

`KspHostCheck "plugin name"`: loads an instrument of the scanned list, plays a note, reports the peak, saves its state as text, loads it
again, restores the state and plays again. Exit 0 when it sounds and the state round-trips.

`KspHostCheck "effect name" --effect`: the same for an effect: a noise burst through it, then through a second instance with the restored state.

`KspHostCheck --record`: the WAV recorder with a synthetic signal: manual mode with the mix and every stem, sync mode (nothing before Start, the music
and the tail after Stop, it closes by itself) and an armed recording that never started; the files are read back and measured.
