# make_hagui_swf.ps1 - regenerate assets\HagUI.swf (the HagUI menu movie).
#
# HagUI needs Bethesda's CLIK input chain (InputDelegate -> FocusHandler -> handleInput) +
# gfx.io.GameDelegate so the engine delivers ESC to AS and AS can call back into our plugin.
# Rather than hand-author that, we CLONE the vanilla CreditsMenu movie (the simplest CLIK menu
# that already ESC-closes) and overwrite just two scripts: the menu class and the root boot.
# We do NOT commit the Bethesda-derived SWF; it is rebuilt on demand from the player's own BSA.
#
# Requires: JDK at C:\dev\jdk, FFDec at C:\dev\ffdec, and the base game installed.
[CmdletBinding()]
param(
    [string]$SkyrimData = 'C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data',
    [string]$Java       = 'C:\dev\jdk\jdk-21.0.11+10\bin\java.exe',
    [string]$Ffdec      = 'C:\dev\ffdec\ffdec.jar'
)
$ErrorActionPreference = 'Stop'
$assets = Join-Path (Split-Path $PSScriptRoot -Parent) 'assets'
$work   = Join-Path $env:TEMP ('hag_hagui_' + $PID)
New-Item -ItemType Directory -Force $assets, $work | Out-Null

# --- extract a named file from the (uncompressed v105) Interface BSA (shared with make_startmenu_swf.ps1) ---
function Extract-BsaFile([string]$bsaPath, [string]$wantName, [string]$outPath) {
    $fs = [System.IO.File]::OpenRead($bsaPath); $br = New-Object System.IO.BinaryReader($fs)
    try {
        $br.ReadBytes(4) | Out-Null
        $ver = $br.ReadUInt32(); if ($ver -ne 105) { throw "unexpected BSA version $ver" }
        $br.ReadUInt32() | Out-Null; $br.ReadUInt32() | Out-Null
        $folderCount = $br.ReadUInt32(); $br.ReadUInt32() | Out-Null
        $br.ReadUInt32() | Out-Null; $totFileName = $br.ReadUInt32()
        $br.ReadUInt16() | Out-Null; $br.ReadUInt16() | Out-Null
        $counts = @()
        for ($i = 0; $i -lt $folderCount; $i++) {
            $br.ReadUInt64() | Out-Null; $c = $br.ReadUInt32(); $br.ReadUInt32() | Out-Null; $br.ReadUInt64() | Out-Null
            $counts += $c
        }
        $files = @()
        foreach ($cnt in $counts) {
            $nl = $br.ReadByte(); $br.ReadBytes($nl) | Out-Null
            for ($j = 0; $j -lt $cnt; $j++) { $br.ReadUInt64() | Out-Null; $sz = $br.ReadUInt32(); $of = $br.ReadUInt32(); $files += [pscustomobject]@{ size = $sz; offset = $of } }
        }
        $names = [System.Text.Encoding]::ASCII.GetString($br.ReadBytes($totFileName)).TrimEnd([char]0).Split([char]0)
        for ($k = 0; $k -lt $files.Count; $k++) { $files[$k] | Add-Member name $names[$k] }
        $t = $files | Where-Object { $_.name -ieq $wantName } | Select-Object -First 1
        if (-not $t) { throw "$wantName not found in $bsaPath" }
        $comp = ($t.size -band 0x40000000) -ne 0; $real = $t.size -band 0x3FFFFFFF
        $fs.Position = $t.offset
        if ($comp) {
            $br.ReadUInt32() | Out-Null; $br.ReadBytes(2) | Out-Null
            $ds = New-Object System.IO.Compression.DeflateStream($fs, [System.IO.Compression.CompressionMode]::Decompress)
            $ms = New-Object System.IO.MemoryStream; $ds.CopyTo($ms); $bytes = $ms.ToArray()
        } else { $bytes = $br.ReadBytes($real) }
        [System.IO.File]::WriteAllBytes($outPath, $bytes)
    } finally { $br.Close(); $fs.Close() }
}

$bsa     = Join-Path $SkyrimData 'Skyrim - Interface.bsa'
$shell   = Join-Path $work 'creditsmenu.swf'
Extract-BsaFile $bsa 'creditsmenu.swf' $shell
Write-Host ("extracted CLIK shell creditsmenu.swf ({0} bytes)" -f (Get-Item $shell).Length)

# --- overwrite exactly two scripts: the menu class + the root boot (FFDec -importScript) ---
$imp = Join-Path $work 'import'
New-Item -ItemType Directory -Force (Join-Path $imp 'scripts\__Packages'), (Join-Path $imp 'scripts\frame_1') | Out-Null
Copy-Item (Join-Path $assets 'HagUI_Menu.as') (Join-Path $imp 'scripts\__Packages\CreditsMenu.as') -Force
Copy-Item (Join-Path $assets 'HagUI_Root.as') (Join-Path $imp 'scripts\frame_1\DoAction.as')        -Force
$out = Join-Path $assets 'HagUI.swf'
& $Java -jar $Ffdec -importScript $shell $out $imp | Out-Null
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ("built {0} ({1} bytes) from the Credits CLIK shell" -f $out, (Get-Item $out).Length)
