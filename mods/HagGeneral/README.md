# HagGeneral

General settings mod for HagLoader/HagUI. It lives under `mods/HagGeneral` in the monorepo and deploys as a
normal Mod Organizer 2 mod:

`<MO2 mods>\HagGeneral\SKSE\Plugins\HagGeneral.dll`

## Runtime

- SKSE accepts the DLL through harmless `SKSEPlugin_*` exports.
- HagLoader scans `Data\SKSE\Plugins`, finds the `SkyrimMod_*` exports, creates the `General` page, then
  calls `SkyrimMod_Init(page)`.
- `HagGeneral` fills that supplied page with its controls and applies settings from `HagGeneral.ini`.

## Settings

| Setting | What it does | Applied via |
|---------|--------------|-------------|
| Fullscreen | Exclusive fullscreen vs windowed | D3D swapchain hook, next launch |
| Borderless | Borderless windowed when not fullscreen | swapchain hook + window restyle |
| Always Active | Keep the game running on focus loss | game setting flag |

## Build

```powershell
.\build.ps1
```

From the repo root, all external mods can be built with:

```powershell
.\mods\build_mods.ps1
```

Pass `-NoCommit` to skip the post-build auto-commit helper.

## Layout

| Path | What |
|------|------|
| `src/Plugin.cpp` | `SkyrimMod_*` contract exports and harmless SKSE compatibility exports |
| `src/Config.*` | Flat INI next to the DLL |
| `src/Display.*` | D3D/window mode handling |
| `src/SettingsHook.*` | Game setting flag patching |
| `src/HagUIBridge.*` | Fills the HagUI page supplied by the loader |
| `src/Offsets.h` | Re-exports `shared/GameOffsets.h` |
