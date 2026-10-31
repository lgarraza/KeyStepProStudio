# Builds KeyStepPro Studio (Release) and its Windows installer, and writes SHA256SUMS.txt next to it in dist\.
#   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1 [-JuceDir F:\path\to\JUCE] [-SkipBuild]
# Needs CMake, Visual Studio 2022 or newer (C++ workload) and Inno Setup 6 (winget install JRSoftware.InnoSetup).
param(
    [string]$JuceDir = "",
    [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# the version comes from CMakeLists.txt: project(KeyStepProStudio VERSION x.y.z ...)
$match = Select-String -Path CMakeLists.txt -Pattern 'project\(KeyStepProStudio VERSION ([0-9.]+)'
if (-not $match) { throw "No version in CMakeLists.txt" }
$version = $match.Matches[0].Groups[1].Value

if (-not $SkipBuild) {
    $configure = @("-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release")
    if ($JuceDir) { $configure += "-DJUCE_DIR=$JuceDir" }
    cmake @configure
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)" }

& $iscc "/DAppVersion=$version" "/DBuildDir=$root\build" "installer\KeyStepProStudio.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

# checksums for the release page
$setup = Join-Path $root "dist\KeyStepProStudio-Setup-$version.exe"
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path -Leaf $setup)" | Set-Content -Encoding ascii (Join-Path $root "dist\SHA256SUMS.txt")
Write-Host "Installer: $setup"
Write-Host "SHA-256:   $hash"
