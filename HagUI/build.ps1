# build.ps1 - build HagUI.dll and deploy it as a Mod Organizer 2 mod.
# MO2 mod folders mirror the game's Data/ root, so:
#   <mods>\HagUI\SKSE\Plugins\HagUI.dll
#   <mods>\HagUI\Interface\HagUI.swf   (once the SWF exists)
[CmdletBinding()]
param(
    [switch]$NoBuild,
    [string]$Mo2Mods = 'C:\Users\Yannis\AppData\Local\ModOrganizer\Skyrim Special Edition\mods',
    [switch]$NoCommit
)
$ErrorActionPreference = 'Stop'
$root  = $PSScriptRoot
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

if (-not $NoBuild) {
    Set-Location $root
    $env:VCPKG_ROOT = 'C:\dev\vcpkg'
    Write-Host '== configure =='
    & $cmake --preset vs2022 | Select-Object -Last 2
    Write-Host '== build =='
    & $cmake --build "$root\build" --config Release | Select-Object -Last 4
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
}

$dll = Get-ChildItem "$root\build" -Recurse -Filter HagUI.dll -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $dll) { throw 'HagUI.dll not found - build first (omit -NoBuild).' }

# --- create the MO2 mod folder architecture ---
$mod     = Join-Path $Mo2Mods 'HagUI'
$plugins = Join-Path $mod 'SKSE\Plugins'
$iface   = Join-Path $mod 'Interface'
New-Item -ItemType Directory -Force $plugins, $iface | Out-Null

Copy-Item $dll.FullName (Join-Path $plugins 'HagUI.dll') -Force
Write-Host "deployed HagUI.dll  -> $plugins"

$swf = Join-Path $root 'assets\HagUI.swf'
if (Test-Path $swf) {
    Copy-Item $swf (Join-Path $iface 'HagUI.swf') -Force
    Write-Host "deployed HagUI.swf  -> $iface"
} else {
    Write-Host '(HagUI.swf not built yet - skipped; drop it at assets\HagUI.swf)'
}

# The main-menu "HagUI" entry is now injected at RUNTIME (GfxInject.cpp, MainMenu tick hook) into the
# live vanilla movie -- no SWF is shipped, so it's SkyUI-safe. Remove any stale modified StartMenu.swf
# so the game uses the vanilla main menu the injector targets.
$staleStart = Join-Path $iface 'StartMenu.swf'
if (Test-Path $staleStart) { Remove-Item $staleStart -Force; Write-Host "removed stale StartMenu.swf (now runtime-injected)" }

# meta.ini so MO2 lists it cleanly
$meta = Join-Path $mod 'meta.ini'
if (-not (Test-Path $meta)) {
    @('[General]', 'gameName=Skyrim Special Edition', 'modid=0', 'version=0.1.0') |
        Set-Content $meta -Encoding UTF8
}

Write-Host "`nMO2 mod ready: $mod"
Get-ChildItem $mod -Recurse -File | ForEach-Object { '  ' + $_.FullName.Substring($mod.Length + 1) }

if (-not $NoCommit) {
    $commit = Join-Path (Split-Path $root -Parent) 'scripts\auto-git-commit.cjs'
    if (Test-Path $commit) { & node $commit }
}
