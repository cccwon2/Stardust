<#
.SYNOPSIS
    Builds Stardust.dll (Win32 Release, TOURNAMENT_BUILD) for StarCraft: Remastered Battle.net
    and copies it with the default stardust_bnet.json into dist\bwapi-data\AI\.

.DESCRIPTION
    Requirements:
      - Visual Studio 2022 or 2026 with "Desktop development with C++" (MSVC v143 / v145, Windows SDK,
        C++ CMake tools for Windows). The newest install is used; override with -Generator.
      - CMake 3.24+ for VS 2022, 4.2+ for VS 2026 (the copy bundled with Visual Studio is found automatically)
      - An existing build directory configured with another generator needs -Clean.

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
    # Optional override, e.g. "Visual Studio 17 2022" or "Visual Studio 18 2026"
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

# Finds the newest Visual Studio 2022 (17.x) or 2026 (18.x) install with the MSVC x86/x64 tools.
# Returns @{ Path; Major; Generator } or $null.
function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }

    $json = & $vswhere -all -prerelease -products * -version "[17.0,19.0)" `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    if (-not $json) { return $null }
    # PowerShell 5.1 emits a parsed JSON array as one object; enumerate it explicitly
    $parsed = ($json | Out-String) | ConvertFrom-Json
    $installs = @($parsed | ForEach-Object { $_ })
    if ($installs.Count -eq 0) { return $null }

    $best = $installs | Sort-Object { [version]$_.installationVersion } -Descending | Select-Object -First 1
    $major = ([version]$best.installationVersion).Major
    $generator = $null
    if ($major -eq 18) { $generator = "Visual Studio 18 2026" }
    elseif ($major -eq 17) { $generator = "Visual Studio 17 2022" }
    if (-not $generator) { return $null }

    return @{ Path = $best.installationPath; Major = $major; Generator = $generator; Name = $best.displayName }
}

# Returns $true if the given cmake.exe lists the generator in its --help output
function Test-CMakeGenerator([string]$CMakePath, [string]$GeneratorName) {
    $ErrorActionPreference = "Continue"
    $help = & $CMakePath --help 2>$null | Out-String
    return ($help -match [regex]::Escape($GeneratorName))
}

function Find-CMake([hashtable]$Vs, [string]$GeneratorName) {
    $candidates = @()

    # Prefer the CMake bundled with the selected Visual Studio (it always supports its own generator)
    if ($Vs) {
        $bundled = Join-Path $Vs.Path "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $bundled) { $candidates += $bundled }
    }

    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }

    $programFilesCMake = Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"
    if (Test-Path $programFilesCMake) { $candidates += $programFilesCMake }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-CMakeGenerator $candidate $GeneratorName) { return $candidate }
        Write-Warning "Skipping $candidate (does not support generator '$GeneratorName')"
    }

    throw ("cmake.exe with generator '$GeneratorName' not found. " +
           "In the Visual Studio Installer, add 'C++ CMake tools for Windows' to the 'Desktop development with C++' workload, " +
           "or install CMake 4.2+ (VS 2026) / 3.24+ (VS 2022) from cmake.org.")
}

# Repository root = parent of this script's folder
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $RepoRoot "CMakeLists.txt"))) {
    throw "Could not find CMakeLists.txt in $RepoRoot"
}

if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $RepoRoot $BuildDir }
if (-not [System.IO.Path]::IsPathRooted($DistDir)) { $DistDir = Join-Path $RepoRoot $DistDir }

$Vs = Find-VisualStudio
if ($Generator -eq "") {
    if (-not $Vs) {
        throw ("No Visual Studio 2022/2026 with MSVC x86/x64 tools found. In the Visual Studio Installer, " +
               "select 'Desktop development with C++' (MSVC build tools, Windows SDK, C++ CMake tools for Windows).")
    }
    $Generator = $Vs.Generator
}
$CMake = Find-CMake $Vs $Generator
if ($Vs) { Write-Host "VS:        $($Vs.Name) ($($Vs.Path))" }
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
