# Deploy only (no build) into the Mod Organizer 2 mods folder.
# Build + deploy is build.ps1; this is the deploy-only shortcut.
& "$PSScriptRoot\build.ps1" -NoBuild @args
