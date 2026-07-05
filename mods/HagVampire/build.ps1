# build.ps1 - build HagVampire.dll and deploy it as a normal Mod Organizer 2 mod.
[CmdletBinding()]
param(
    [switch]$NoBuild,
    [string]$Mo2Mods = 'C:\Users\Yannis\AppData\Local\ModOrganizer\Skyrim Special Edition\mods'
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

$dll = Get-ChildItem "$root\build" -Recurse -Filter HagVampire.dll -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $dll) { throw 'HagVampire.dll not found - build first (omit -NoBuild).' }

$mod     = Join-Path $Mo2Mods 'HagVampire'
$plugins = Join-Path $mod 'SKSE\Plugins'
New-Item -ItemType Directory -Force $plugins | Out-Null
Copy-Item $dll.FullName (Join-Path $plugins 'HagVampire.dll') -Force
Write-Host "deployed HagVampire.dll -> $plugins"

$meta = Join-Path $mod 'meta.ini'
if (-not (Test-Path $meta)) {
    @('[General]', 'gameName=Skyrim Special Edition', 'modid=0', 'version=0.1.0') |
        Set-Content $meta -Encoding UTF8
}

Write-Host "`nMO2 mod ready: $mod"
Get-ChildItem $mod -Recurse -File | ForEach-Object { '  ' + $_.FullName.Substring($mod.Length + 1) }

$commit = Join-Path (Split-Path (Split-Path $root -Parent) -Parent) 'scripts\auto-git-commit.cjs'
if (Test-Path $commit) {
    & node $commit
    if ($LASTEXITCODE -ne 0) { throw "auto commit/push failed (exit $LASTEXITCODE)" }
}
