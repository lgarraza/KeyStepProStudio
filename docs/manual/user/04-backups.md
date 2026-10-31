# 4. Backups

**You will:** know where your safety copies are and how to use them.

## Automatic

Before every write to a slot the app saves that slot to
`Documents\KeyStepPro Studio\Backups\ksp-pre-restore-P<slot>-<date>.json`. Nothing to do.

## Full backup of the device

Use the command-line tool `KspBackup` (installed with the app, read-only):

```bash
KspBackup.exe
```

It reads the global settings and all 16 projects (about 10 minutes) into `Backups\`. The JSON is the same format as
[KSCON](https://github.com/canadianblaken/keystep-con), so either tool opens it.

## Use a backup

- **Open backup...** in the app loads a backup file; the project with content is picked. Edit it, then **Save** to the
  Library or **Write to slot**.
- To put one project back without the editor: `KspRestore --file X --project N [--to M] [--write]`
  (without `--write` it is a dry run).

**Watch out:** make one full backup before your first write, and keep the `Backups` folder; the app never deletes it.
