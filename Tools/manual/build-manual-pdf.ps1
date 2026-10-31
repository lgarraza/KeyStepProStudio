<#
 Builds the user manual PDF from the Markdown chapters in docs/manual/user (Markdown stays the source of truth).
 Needs pandoc and a Chromium-based browser (Microsoft Edge ships with Windows; Chrome works too):
   winget install JohnMacFarlane.Pandoc
 Mermaid diagrams are drawn by mermaid.js (pandoc embeds it from jsDelivr, so the build needs the network).
 Output: dist\KeyStepPro-Studio-Manual.pdf  (installer\build-installer.ps1 calls this and ships the file next to the exe)
#>
param([string]$Out = "")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
if (-not $Out) { $Out = Join-Path $root "dist\KeyStepPro-Studio-Manual.pdf" }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null

$pandoc = (Get-Command pandoc -ErrorAction SilentlyContinue).Source
if (-not $pandoc) { throw "pandoc not found (winget install JohnMacFarlane.Pandoc)" }
$browser = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $browser) { throw "Edge or Chrome not found" }

$dir = "docs\manual\user"
$chapters = @("$dir\README.md") + (Get-ChildItem $dir -Filter "*.md" | Where-Object Name -match '^\d\d-' | Sort-Object Name | ForEach-Object { "$dir\$($_.Name)" })
$html = Join-Path ([IO.Path]::GetTempPath()) "ksp-manual.html"
& $pandoc -f gfm -t html5 --standalone --embed-resources `
    --metadata title="KeyStepPro Studio - User guide" --css Tools\manual\manual.css `
    --lua-filter Tools\manual\strip-md-links.lua --lua-filter Tools\manual\mermaid.lua `
    --include-in-header Tools\manual\mermaid-header.html --resource-path $dir @chapters -o $html
if ($LASTEXITCODE -ne 0) { throw "pandoc failed" }

if (Test-Path $Out) { Remove-Item $Out -Force }
$uri = ([Uri](Resolve-Path $html).Path).AbsoluteUri
& $browser --headless --disable-gpu --no-pdf-header-footer --virtual-time-budget=20000 "--print-to-pdf=$Out" $uri | Out-Null
# msedge.exe returns at once and prints in the background: wait until the PDF is there and stops growing
$size = -1
for ($i = 0; $i -lt 120; $i++) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $Out) {
        $now = (Get-Item $Out).Length
        if ($now -gt 0 -and $now -eq $size) { break }
        $size = $now
    }
}
if (-not (Test-Path $Out)) { throw "PDF was not created" }
Write-Host "Manual: $Out"
