# Runs the Windows build: the PowerShell counterpart of launch.sh (which runs the
# Linux one).  Builds first, sets up run\ and starts the game there.
#
#   .\launch.ps1 [-Headless] [-SkipLauncher] [-AutoExit <seconds>] [-Config RelWithDebInfo]
#
# Needs a configured build\ (cmake --preset windows) and the imported game\.
param(
    [switch]$Headless,       # no window, no graphics; implies -SkipLauncher
    [switch]$SkipLauncher,   # take the video mode from openroo.ini
    [int]$AutoExit = 0,      # exit after this many seconds
    [string]$Config = "RelWithDebInfo",
    [string]$BuildDir = $(if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "build" })
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
if ($Headless) { $SkipLauncher = $true }

if (-not $env:OPENROO_SKIP_BUILD) {
    cmake --build $BuildDir --config $Config -- /nologo /v:m
    if ($LASTEXITCODE) { Write-Error "build failed"; exit 1 }
}
$exe = Join-Path $BuildDir "$Config\OpenRoo.exe"
if (-not (Test-Path $exe)) { Write-Error "$exe missing after build"; exit 1 }
if (-not (Test-Path game)) { Write-Error "game\ missing - run tools\import_assets.py --from <your Ka'roo>, or tools\gen_assets.py for placeholders"; exit 1 }

# run\ holds what the game writes plus links to each read-only entry in game\.
New-Item -ItemType Directory -Force run, run\SavedGames, run\highscores | Out-Null
foreach ($entry in Get-ChildItem game) {
    if ($entry.Name -in "SavedGames", "highscores", "ProgableControl.sav") { continue }
    $link = Join-Path run $entry.Name
    if (Test-Path $link) { continue }
    if ($entry.PSIsContainer) {
        New-Item -ItemType Junction -Path $link -Target $entry.FullName | Out-Null
    } else {
        New-Item -ItemType HardLink -Path $link -Target $entry.FullName | Out-Null
    }
}
# The executable and the DLLs vcpkg put beside it.
$out = Join-Path $BuildDir $Config
Copy-Item "$out\*.exe", "$out\*.dll" run -Force
if (-not (Test-Path run\openroo.ini)) {
    Write-Host "run\openroo.ini missing - installing data\openroo.ini.default (1024x768x32)"
    Copy-Item data\openroo.ini.default run\openroo.ini
}

# Slow-frame detection on by default, as in launch.sh.
if ($null -eq $env:KAROO_SLOWFRAME_MS) { $env:KAROO_SLOWFRAME_MS = "16" }
$env:KAROO_SKIP_LAUNCHER = [int]$SkipLauncher.IsPresent
$env:KAROO_AUTO_EXIT_SECS = $AutoExit
$env:KAROO_HEADLESS = [int]$Headless.IsPresent

Set-Location run
foreach ($log in "karoo_hooks.log") { if (Test-Path $log) { Move-Item -Force $log "$log.1" } }
$p = Start-Process -FilePath .\OpenRoo.exe -Wait -PassThru
exit $p.ExitCode
