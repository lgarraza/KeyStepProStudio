# 1. Install and first run

**You will:** install the app, start it, and see the main window.

## Install (Windows)

1. Download `KeyStepProStudio-Setup-x.y.z.exe` from the [Releases](../../../../../releases) page and run it.
   No installer wanted? Download `KeyStepProStudio-x.y.z-portable.zip`, unzip it anywhere and run `KeyStepPro Studio.exe`
   (your data still goes to `Documents\KeyStepPro Studio\`).
2. Windows SmartScreen says *"Windows protected your PC"* because the file is not code-signed (see [signing](../../signing.md)).
   Click **More info → Run anyway**.
3. Optional: check the download. Open PowerShell in the folder and run
   `Get-FileHash .\KeyStepProStudio-Setup-x.y.z.exe -Algorithm SHA256`; the result must equal the line for that file in
   `SHA256SUMS.txt` on the release page. The release notes also link the VirusTotal scans.
4. Start **KeyStepPro Studio** from the Start menu (installer) or run the exe (zip).

The installer also adds `KspVstHelper.exe` (VST3 helper, Full edition) and the command-line tools. That helper only comes with the **Full** edition (`KeyStepProStudio-Full-Setup-x.y.z.exe`). We recommend the **Lite** edition (`KeyStepProStudio-Setup-x.y.z.exe`), which has no VST3 parts; installing one edition replaces the other.

## Install (macOS)

1. Download `KeyStepProStudio-x.y.z-mac.zip` (Lite, recommended) or `KeyStepProStudio-Full-x.y.z-mac.zip` from the
   [Releases](../../../../../releases) page and unzip it. The app is universal (Apple silicon and Intel), macOS 11 or later.
2. Drag `KeyStepPro Studio.app` to **Applications**.
3. The app is signed ad hoc, not notarized by Apple, so macOS says *"KeyStepPro Studio Not Opened - Apple could not verify..."*.
   This is expected. Click **Done** (not *Move to Trash*), then open **System Settings > Privacy & Security**, scroll to the
   message about KeyStepPro Studio and click **Open Anyway**; enter your password and open the app again. You only do this once.
   On older macOS, right-click the app > **Open** > **Open** is enough. From Terminal you can also run
   `xattr -dr com.apple.quarantine "KeyStepPro Studio.app"`.
4. Check the download: `shasum -a 256 KeyStepProStudio-x.y.z-mac.zip` must equal the `.sha256` file on the release page.

Data goes to `~/Documents/KeyStepPro Studio/`.

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

**Watch out:** the first time you run **Scan plugins**, Windows may ask once to allow `KspVstHelper.exe`. Allow it.
