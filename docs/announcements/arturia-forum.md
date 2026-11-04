# Arturia forum announcement (draft)

Target board: https://legacy-forum.arturia.com/index.php?board=378.0

**Not posted yet.** Leo posts it himself, after Microsoft clears the Defender false positive on the v0.4.1 installer
(then update the "Windows warnings" paragraph and the release link if a newer release exists).
Copy everything between the two lines below. It uses plain BBCode (`[b]`, `[url]`, `[img]`, `[list]`, `[code]`).

---

## Title

KeyStepPro Studio: free, open-source editor and librarian for the KeyStep Pro (Windows)

## Body

```
Hi everyone,

I made a free, open-source desktop app for the KeyStep Pro and I'd like to share it and get feedback from other owners.

[b]KeyStepPro Studio[/b] lets you edit your KeyStep Pro projects on a big screen and keep them in a library on your computer.

[b]What it does[/b]
[list]
[li]Read and write projects over USB (SysEx), with the KeyStep's 16 slots used as working memory.[/li]
[li]Full backups of all projects and global settings to JSON.[/li]
[li]Grid editor: all 64 steps of every pattern, notes with pitch, gate, velocity, time shift and randomness, length, root and scale, copy patterns, overdub recording.[/li]
[li]Drum grid for track 1 in drum mode.[/li]
[li]Project Map: scenes x tracks with a mini piano roll in each cell. Pick the pattern a track plays in a scene, copy / paste / clear scenes, Undo.[/li]
[li]Generate: writes patterns and scenes into the open project in a style (Ambient, Techno, House, Drum & bass), with a seed and undo.[/li]
[li]VST3 host: one instrument per track (T1-T4 on MIDI channels 1-4), effect inserts and a master chain. The KeyStep is the clock, so you can play it without a DAW. Optional WAV recording of the mix or one stem per track.[/li]
[li]Illustrated user manual (PDF) included.[/li]
[/list]

[b]Safety[/b]
[list]
[li]Writes only to a KeyStep Pro on [b]firmware 2.5.20[/b], the only version I have tested. On any other firmware the app can read but refuses to write.[/li]
[li]Before every write it backs up the slot automatically, and afterwards it reads the slot back and compares every value.[/li]
[li]It reads what is saved in flash, not the live state, so save your project on the KeyStep first.[/li]
[li]Please still make a full backup before you start.[/li]
[/list]

[b]Platforms[/b]
Windows now. A Mac version is in progress but not released yet.

[b]Windows warnings (please read)[/b]
The installer is [b]not code-signed[/b] (code-signing certificates are expensive for a free hobby project), so Windows SmartScreen may say "Windows protected your PC": click More info > Run anyway. Some Defender versions have also flagged the installer with a machine-learning false positive ("Trojan:Win32/Wacatac.C!ml"); I have reported it to Microsoft. If you prefer not to take chances, use the [b]portable zip[/b] (unzip anywhere, run "KeyStepPro Studio.exe"). Every release lists the SHA-256 hashes and VirusTotal scan links in its notes, so you can check that what you downloaded is exactly what I published. The full source code is public, so you can also build it yourself.

[b]Download[/b]
[url=https://github.com/lgarraza/KeyStepProStudio/releases/latest]Latest release (v0.4.1)[/url]
[url=https://github.com/lgarraza/KeyStepProStudio]Source code and docs on GitHub[/url] (AGPL-3.0)

[b]Bug reports and feedback[/b]
This is a hobby project and I only own one KeyStep Pro, so your reports are really valuable. If something goes wrong, please open an issue on GitHub (or reply here) with: your firmware version, Windows version, what you did, and what you expected. Attach the backup the app made, if relevant.

[b]Disclaimer[/b]
KeyStepPro Studio is an unofficial community project, not affiliated with, endorsed by or supported by Arturia. It talks to the KeyStep Pro with an undocumented protocol. Use it at your own risk, no warranty.

Thanks for reading, and happy sequencing!

[img]PASTE_SCREENSHOT_URL_1[/img]
```

---

## Screenshots to attach

The forum needs images hosted somewhere (upload to the post as attachments, or use the GitHub raw URLs after merge).
Existing files in this repo:

| # | File | Shows |
| --- | --- | --- |
| 1 | `docs/images/main-window.png` | Main window with the grid editor (lead image) |
| 2 | `docs/images/project-map.png` | Project Map: scenes by tracks |
| 3 | `docs/images/fx-window.png` | Effects window: four inserts per track and master chain |

Raw URLs once on `main`:
`https://raw.githubusercontent.com/lgarraza/KeyStepProStudio/main/docs/images/main-window.png` (same pattern for the others).

Optional extra shots from `docs/manual/user/img/` (for example the write confirmation dialog, which shows the safety
checks) can be added; keep the post to three or four images.

## Before posting

- Check that Microsoft has answered the false-positive submission, then soften or remove the Defender sentence.
- If a newer release than v0.4.1 exists, update the release name in the Download section.
- Re-read the board rules; reply to questions in English.
