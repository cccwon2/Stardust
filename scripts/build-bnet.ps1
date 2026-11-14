<#
.SYNOPSIS
    Builds Stardust.dll (Win32 Release, TOURNAMENT_BUILD) for StarCraft: Remastered Battle.net
    and copies it with the default stardust_bnet.json into dist\bwapi-data\AI\.

.DESCRIPTION
    Requirements:
      - Visual Studio 2022 with "Desktop development with C++" (MSVC v143, Windows SDK)
      - CMake 3.24 or newer (the copy bundled with Visual Studio is found automatically)

    Compatible with Windows PowerShell 5.1. ASCII only.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build-bnet.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build-bnet.ps1 -Clean -NoBnetLog
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build-bnet",
    [string]$DistDir = "dist",
    [switch]$Clean,
    [switch]$VsHuman,
    [switch]$NoBnetLog,
    [switch]$OverwriteConfig
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }

    throw "cmake.exe not found. Install CMake 3.24+ or Visual Studio 2022 with C++ CMake tools."
}

# Repository root = parent of this script's folder
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $RepoRoot "CMakeLists.txt"))) {
    throw "Could not find CMakeLists.txt in $RepoRoot"
}

if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $RepoRoot $BuildDir }
if (-not [System.IO.Path]::IsPathRooted($DistDir)) { $DistDir = Join-Path $RepoRoot $DistDir }

$CMake = Find-CMake
Write-Host "CMake:     $CMake"
Write-Host "Repo:      $RepoRoot"
Write-Host "Build dir: $BuildDir"
Write-Host "Dist dir:  $DistDir"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

# Configure
$vsHumanValue = "OFF"
if ($VsHuman) { $vsHumanValue = "ON" }
$bnetLogValue = "ON"
if ($NoBnetLog) { $bnetLogValue = "OFF" }

Write-Step "Configuring (Visual Studio 17 2022, Win32, TOURNAMENT_BUILD=ON, VS_HUMAN=$vsHumanValue, BNET_LOG=$bnetLogValue)"
& $CMake -S $RepoRoot -B $BuildDir -G "Visual Studio 17 2022" -A Win32 `
    "-DTOURNAMENT_BUILD=ON" `
    "-DVS_HUMAN=$vsHumanValue" `
    "-DBNET_LOG=$bnetLogValue"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit code $LASTEXITCODE)" }

# Build only the Stardust target (TOURNAMENT_BUILD also skips the windbg target)
Write-Step "Building target Stardust (Release)"
& $CMake --build $BuildDir --config Release --target Stardust -- /m /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)" }

# Locate the DLL
$dll = Join-Path $BuildDir "src\Release\Stardust.dll"
if (-not (Test-Path $dll)) {
    $found = Get-ChildItem -Path $BuildDir -Recurse -Filter "Stardust.dll" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\Release\\" } | Select-Object -First 1
    if (-not $found) { throw "Stardust.dll not found under $BuildDir" }
    $dll = $found.FullName
}

# Deploy
$aiDir = Join-Path $DistDir "bwapi-data\AI"
$readDir = Join-Path $DistDir "bwapi-data\read"
$writeDir = Join-Path $DistDir "bwapi-data\write"
foreach ($dir in @($aiDir, $readDir, $writeDir)) {
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
}

Write-Step "Copying Stardust.dll"
Copy-Item -Force $dll (Join-Path $aiDir "Stardust.dll")

$template = Join-Path $RepoRoot "config\stardust_bnet.json"
$targetConfig = Join-Path $aiDir "stardust_bnet.json"
$defaultConfig = Join-Path $aiDir "stardust_bnet.default.json"
if (Test-Path $template) {
    # Always refresh the reference copy of the defaults
    Copy-Item -Force $template $defaultConfig
    if ((-not (Test-Path $targetConfig)) -or $OverwriteConfig) {
        Write-Step "Copying stardust_bnet.json template"
        Copy-Item -Force $template $targetConfig
    }
    else {
        Write-Step "Keeping existing stardust_bnet.json (use -OverwriteConfig to replace); defaults written to stardust_bnet.default.json"
    }
}
else {
    Write-Warning "Template not found: $template (the bot will use built-in defaults)"
}

$info = Get-Item (Join-Path $aiDir "Stardust.dll")
Write-Step "Done"
Write-Host ("Stardust.dll  {0:N0} bytes  {1}" -f $info.Length, $info.LastWriteTime)
Write-Host "Output: $aiDir"
Write-Host "Copy the contents of $DistDir\bwapi-data into your StarCraft bwapi-data folder."
