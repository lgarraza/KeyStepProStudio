# Code signing and antivirus warnings

The Windows installer and the app are not code-signed yet. What that means and what can be done about it.

## What users see

- **SmartScreen** ("Windows protected your PC") on any downloaded exe that has no reputation yet. It is not a virus warning:
  **More info, Run anyway** starts it. Reputation builds up per file as more people download and run it.
- **Antivirus false positives** are rarer. Small unsigned tools that spawn child processes (the plugin scanner does) and talk to
  USB devices sometimes get flagged by heuristics.

## Options

| Option | Cost | Removes SmartScreen? | Notes |
| --- | --- | --- | --- |
| Publish SHA-256 checksums and a VirusTotal link with each release | free | no | Lets users check the file is the one published. Done by `installer/build-installer.ps1`. |
| Submit a flagged file to Microsoft (https://www.microsoft.com/wdsi/filesubmission) | free | no, fixes Defender detections | Use "software developer" and "incorrectly detected"; usually cleared in a few days. Other vendors have similar forms. |
| SignPath Foundation | free for open source | gradually | Applied for; the application was not accepted, so it is not an option for now. |
| Azure Trusted Signing (Artifact Signing) | about 10 USD a month | gradually | Microsoft's own service. Open to organisations in some regions and to individual developers only in the US and Canada at the time of writing: check eligibility first. |
| OV code-signing certificate (Sectigo, DigiCert, Certum...) | about 100-400 USD a year | gradually | The key must live on a hardware token or cloud HSM (rule since 2023). Certum's open source certificate is the cheapest. |
| EV certificate | 300-600 USD a year | no longer instantly (Microsoft dropped the EV shortcut in 2024) | Not worth it for a free tool. |

No option makes the first downloads of a new file warning-free: since 2024 even signed files start with no reputation. Signing
helps because reputation then belongs to the signer and carries over to the next release.

## Where this stands

Releases are unsigned, with SHA-256 checksums and a VirusTotal scan link in the release notes, and the SmartScreen
instructions in the README. If Defender ever flags a release, submit it to Microsoft. Signing may be revisited later
(for example with a paid certificate); until then the checksums are the way to verify a download.
