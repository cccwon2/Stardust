<#
.SYNOPSIS
    Builds Stardust.dll (Win32 Release, TOURNAMENT_BUILD) for StarCraft: Remastered Battle.net
    and copies it with the default stardust_bnet.json into dist\bwapi-data\AI\.

.DESCRIPTION
    Requirements:
      - Visual Studio 2022 or 2026 with "Desktop development with C++" (MSVC x86/x64 tools, Windows SDK)
      - CMake 3.24 or newer, 4.2+ for Visual Studio 2026 (the copy bundled with Visual Studio is preferred)
    The newest installed Visual Studio 2022/2026 selects the generator; override with -Generator.

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
    [switch]$OverwriteConfig,
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

# Returns @{ Path; Major } for the newest Visual Studio 2022 (17) or 2026 (18) with the x86/x64 C++ tools, or $null
function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }

    $vsPath = & $vswhere -latest -version "[17.0,19.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vsVersion = & $vswhere -latest -version "[17.0,19.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    if ((-not $vsPath) -or (-not $vsVersion)) { return $null }

    $vsPath = @($vsPath)[0]
    $major = [int](@($vsVersion)[0].Split('.')[0])
    return @{ Path = $vsPath; Major = $major }
}

function Get-Generator([int]$Major) {
    switch ($Major) {
        17 { return "Visual Studio 17 2022" }
        18 { return "Visual Studio 18 2026" }
        default { throw "Unsupported Visual Studio major version $Major" }
    }
}

function Find-CMake($Vs) {
    # Prefer the CMake bundled with the selected Visual Studio: it always knows that Visual Studio's generator
    if ($Vs) {
        $candidate = Join-Path $Vs.Path "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $candidate) { return $candidate }
    }

    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    throw "cmake.exe not found. Install CMake 3.24+ (4.2+ for Visual Studio 2026) or the C++ CMake tools component of Visual Studio."
}

# Repository root = parent of this script's folder
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $RepoRoot "CMakeLists.txt"))) {
    throw "Could not find CMakeLists.txt in $RepoRoot"
}

if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $RepoRoot $BuildDir }
if (-not [System.IO.Path]::IsPathRooted($DistDir)) { $DistDir = Join-Path $RepoRoot $DistDir }

$Vs = Find-VisualStudio
if (-not $Vs) {
    throw "Visual Studio 2022 or 2026 with 'Desktop development with C++' (MSVC x86/x64 tools) was not found."
}
if (-not $Generator) { $Generator = Get-Generator $Vs.Major }
$CMake = Find-CMake $Vs
Write-Host "VS:        $($Vs.Path) (major $($Vs.Major))"
Write-Host "Generator: $Generator"
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

# A build dir configured with another generator cannot be reused
$cache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cache) {
    $cachedGenerator = Select-String -Path $cache -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' | Select-Object -First 1
    if ($cachedGenerator -and ($cachedGenerator.Matches[0].Groups[1].Value -ne $Generator)) {
        throw "$BuildDir was configured with '$($cachedGenerator.Matches[0].Groups[1].Value)'. Re-run with -Clean to switch to '$Generator'."
    }
}

Write-Step "Configuring ($Generator, Win32, TOURNAMENT_BUILD=ON, VS_HUMAN=$vsHumanValue, BNET_LOG=$bnetLogValue)"
& $CMake -S $RepoRoot -B $BuildDir -G $Generator -A Win32 `
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
