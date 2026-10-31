# 1. Install and first run

**You will:** install the app, start it, and see the main window.

## Install (Windows)

1. Download `KeyStepProStudio-Setup-x.y.z.exe` from the [Releases](../../../../../releases) page and run it.
2. Windows SmartScreen says *"Windows protected your PC"* because the installer is not code-signed.
   Click **More info → Run anyway** (see [signing](../../signing.md); each release lists the SHA-256 of its files so you can check the download).
3. Start **KeyStepPro Studio** from the Start menu.

The installer also adds `KspScanner.exe` (plugin scanner) and the command-line tools.

## First run

- With no KeyStep connected, the app opens your most recent **Library** project. On a fresh install the library is empty,
  so use **Open backup...** or connect the KeyStep ([chapter 2](02-connect.md)).
- Everything the app stores is in `Documents\KeyStepPro Studio\`:

| Folder / file | What |
| --- | --- |
| `Library\` | your projects (`.kspstudio` + `index.json`) |
| `Backups\` | automatic backups of slots, made before each write |
| `Recordings\` | WAV files |
| `KnownPlugins.xml`, `HiddenPlugins.xml` | the VST3 list |

![Main window](img/shot-first-run.png)

**Watch out:** the first time you run **Scan plugins**, Windows may ask once to allow `KspScanner.exe`. Allow it.
