# Windows installer

`KeyStepProStudio.iss` is an [Inno Setup 6](https://jrsoftware.org/isinfo.php) script. It installs the app and the plugin
scanner in `%LOCALAPPDATA%\Programs\KeyStepPro Studio` (or Program Files if "all users" is chosen), the command-line tools
in its `tools` folder, a Start menu entry and an uninstaller. The user's data in `Documents\KeyStepPro Studio` (library,
backups, recordings, plugin list) is never touched by the installer or the uninstaller.

## Build it

Needs CMake, Visual Studio 2022 or newer with the C++ workload, and Inno Setup 6 (`winget install JRSoftware.InnoSetup`).

```
powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
```

It builds Release into `build\`, compiles the installer into `dist\KeyStepProStudio-Setup-<version>.exe` and writes
`dist\SHA256SUMS.txt`. Options: `-JuceDir <path>` to use a local JUCE, `-SkipBuild` to package what is already built.
The version comes from `project(... VERSION x.y.z)` in `CMakeLists.txt`.

## Release

1. Bump the version in `CMakeLists.txt`, build the installer.
2. Upload the setup exe to [VirusTotal](https://www.virustotal.com) and keep the link.
3. Create a GitHub release with the setup exe and `SHA256SUMS.txt`, and the VirusTotal link in the notes.

The installer is not code-signed; see [../docs/signing.md](../docs/signing.md).
