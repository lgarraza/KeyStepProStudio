# Releasing

The release is built on Windows by hand and is **not code-signed** (see [signing.md](signing.md)). To keep antivirus and
SmartScreen warnings low, every release ships a portable zip next to the installer, SHA-256 checksums, a VirusTotal link and,
when needed, a Microsoft false-positive submission.

## 1. Build

1. Merge the release PR (version in `CMakeLists.txt`, `docs/release-notes/vX.Y.Z.md`, README).
2. On Windows, from the repo root, with `main` up to date:
   ```
   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
   ```
3. `dist\` now holds:
   - `KeyStepProStudio-Setup-X.Y.Z.exe` (installer)
   - `KeyStepProStudio-X.Y.Z-portable.zip` (same files, no installer; unzip anywhere and run `KeyStepPro Studio.exe`)
   - `SHA256SUMS.txt` (SHA-256 of both, one line each)

The portable zip matters for the forums: it needs no installer, and many people prefer to inspect a zip before running anything.

## 2. Scan with VirusTotal (needs your account)

1. Sign in at https://www.virustotal.com and upload `KeyStepProStudio-Setup-X.Y.Z.exe`; then the portable zip.
2. Wait for the scan to finish. A couple of low-reputation heuristic hits ("Generic", "ML", "Unsafe") out of about 70 engines
   are common for unsigned small tools; a named malware family is not, and means stop and investigate.
3. Copy the report URL of each file (the address bar after the scan, `https://www.virustotal.com/gui/file/<sha256>`).
   The hash in the URL must match `SHA256SUMS.txt`.

## 3. Publish the release

1. Tag `vX.Y.Z` on the merge commit (new tag per release; never move or re-create a published tag).
2. Create the GitHub release with the three files from `dist\` and use `docs/release-notes/vX.Y.Z.md` as the body.
3. Add this to the notes, with the real links:
   ```
   ## Checks
   Not code-signed (Windows SmartScreen will warn). Verify the download against SHA256SUMS.txt.
   VirusTotal: [installer](https://www.virustotal.com/gui/file/<sha256>) · [portable zip](https://www.virustotal.com/gui/file/<sha256>)
   ```
4. If the VirusTotal links were not ready yet, publish and edit the notes afterwards.

## 4. If an antivirus flags it

Only if a user reports a detection or VirusTotal shows a named one:

1. **Microsoft Defender**: https://www.microsoft.com/wdsi/filesubmission, sign in with a Microsoft account.
   Choose **Software developer**, then **Incorrectly detected as malware/malicious** (or "Incorrectly detected as PUA" when that is what Defender said).
   Upload the installer (and the zip if it is flagged), give the detection name shown by Defender, and say it is open source
   with the repo URL. Answers usually arrive in a few days; the file hash is then cleared in Defender's definitions.
2. **Other vendors** (the ones that flagged it in VirusTotal): most have a false-positive form, listed by VirusTotal on the
   engine's name. Submit the same file and the repo URL.
3. When cleared, rescan on VirusTotal and update the links in the release notes.

SmartScreen is a separate system based on reputation, not detections: it cannot be cleared by a submission, only by downloads
over time or by signing.

## Checklist

- [ ] Release PR merged, tag created
- [ ] `build-installer.ps1` run, three files in `dist\`
- [ ] Both files on VirusTotal, links in the release notes
- [ ] GitHub release published with `SHA256SUMS.txt`
- [ ] `main-window.png` refreshed (once per release, see the README)
- [ ] Defender submission done if anything was flagged
