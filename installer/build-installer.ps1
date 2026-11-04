# Builds KeyStepPro Studio (Release), its Windows installer and a portable zip, and writes SHA256SUMS.txt (both files) next to it in dist\.
#   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1 [-JuceDir F:\path\to\JUCE] [-SkipBuild] [-Lite]
#   -Lite builds the edition without VST3 hosting (build-lite\, KeyStepProStudio-Lite-*).
# Needs CMake, Visual Studio 2022 or newer (C++ workload) and Inno Setup 6 (winget install JRSoftware.InnoSetup)
# and pandoc (winget install JohnMacFarlane.Pandoc) for the manual PDF; Edge or Chrome renders it.
param(
    [string]$JuceDir = "",
    [switch]$SkipBuild,
    [switch]$Lite
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# the version comes from CMakeLists.txt: project(KeyStepProStudio VERSION x.y.z ...)
$match = Select-String -Path CMakeLists.txt -Pattern 'project\(KeyStepProStudio VERSION ([0-9.]+)'
if (-not $match) { throw "No version in CMakeLists.txt" }
$version = $match.Matches[0].Groups[1].Value
$buildName = if ($Lite) { "build-lite" } else { "build" }
$edition = if ($Lite) { "-Lite" } else { "" }

if (-not $SkipBuild) {
    $configure = @("-S", ".", "-B", $buildName, "-DCMAKE_BUILD_TYPE=Release")
    if ($Lite) { $configure += "-DKSP_ENABLE_VST_HOST=OFF" }
    if ($JuceDir) { $configure += "-DJUCE_DIR=$JuceDir" }
    cmake @configure
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build $buildName --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

# the user manual PDF (built from docs/manual/user; the installer and the zip ship it next to the exe)
& powershell -ExecutionPolicy Bypass -File (Join-Path $root "Tools\manual\build-manual-pdf.ps1")
if ($LASTEXITCODE -ne 0) { throw "Manual PDF failed" }

$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)" }

$isccArgs = @("/DAppVersion=$version", "/DBuildDir=$root\$buildName")
if ($Lite) { $isccArgs += "/DLite=1" }
& $iscc @isccArgs "installer\KeyStepProStudio.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

# portable zip: the same files the installer copies, no installer needed (unzip anywhere and run)
$stageName = "KeyStepProStudio$edition-$version-portable"
$stage = Join-Path $root "dist\$stageName"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path "$stage\tools" | Out-Null
$bd = Join-Path $root $buildName
Copy-Item "$bd\KeyStepProStudio_artefacts\Release\KeyStepPro Studio.exe" $stage
if (-not $Lite) { Copy-Item "$bd\KspVstHelper_artefacts\Release\KspVstHelper.exe" $stage }
$tools = @("KspProbe", "KspBackup", "KspRestore", "KspGen")
if (-not $Lite) { $tools += "KspHostCheck" }
foreach ($t in $tools) {
    Copy-Item "$bd\${t}_artefacts\Release\$t.exe" "$stage\tools"
}
Copy-Item README.md, THIRD_PARTY_NOTICES.md $stage
Copy-Item (Join-Path $root "dist\KeyStepPro-Studio-Manual.pdf") $stage
Copy-Item LICENSE (Join-Path $stage "LICENSE.txt")
$zip = Join-Path $root "dist\$stageName.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip
Remove-Item $stage -Recurse -Force

# checksums for the release page (installer and portable zip, in the format `sha256sum -c` and Get-FileHash users expect)
$setup = Join-Path $root "dist\KeyStepProStudio$edition-Setup-$version.exe"
$lines = foreach ($f in $setup, $zip) {
    "$((Get-FileHash $f -Algorithm SHA256).Hash.ToLower())  $(Split-Path -Leaf $f)"
}
$lines | Set-Content -Encoding ascii (Join-Path $root "dist\SHA256SUMS$edition.txt")
Write-Host "Installer: $setup"
Write-Host "Portable:  $zip"
$lines | ForEach-Object { Write-Host "SHA-256:   $_" }
