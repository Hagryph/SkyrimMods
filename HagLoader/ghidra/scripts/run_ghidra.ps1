# run_ghidra.ps1 - run a Ghidra headless postScript against the analyzed (unpacked) SkyrimSE.
#   .\run_ghidra.ps1 Trace.java <outfile> <mode> <targets...>
param(
    [Parameter(Mandatory=$true)][string]$ScriptName,
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$ScriptArgs
)
$ErrorActionPreference = 'Stop'
$headless = 'C:\dev\ghidra\ghidra_12.1.2_PUBLIC\support\analyzeHeadless.bat'
$proj     = 'C:\dev\re\ghidra-proj'
$scripts  = $PSScriptRoot
& $headless $proj SkyrimSE -process 'SkyrimSE.exe.unpacked.exe' -noanalysis -scriptPath $scripts -postScript $ScriptName @ScriptArgs
