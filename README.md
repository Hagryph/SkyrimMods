# SkyrimMods

Single-repo workspace for Hagryph's Skyrim Special Edition AE 1.6.1170 mods.

## Architecture

This follows the `SoWMods` layout, adapted for Skyrim and Mod Organizer 2:

| Path | Purpose |
|------|---------|
| `HagUI/` | Main SKSE plugin: loader, shared UI host, HagUI panel, external mod discovery. |
| `mods/` | External feature mods. Each folder is its own CMake project. |
| `mods/HagGeneral/` | Ported General settings mod. HagUI creates its page and calls `SkyrimMod_Init`. |
| `shared/` | Cross-mod headers: offsets, HagUI ABI, Skyrim mod-loader ABI. |
| `HagIPC/` | Dev/debug SKSE plugin and IPC tooling. |
| `tools/` | Reverse-engineering scripts and helpers. |
| `.claude/` | Single-repo auto-commit hook. |

## Runtime Model

- `HagUI.dll` is the main loader/UI mod.
- Feature DLLs still deploy as normal Skyrim/MO2 mods:
  `<MO2 mods>\<ModName>\SKSE\Plugins\<ModName>.dll`.
- At runtime, MO2 merges those into `Data\SKSE\Plugins`. HagUI scans that normal plugin folder,
  skips itself, ignores DLLs without the `SkyrimMod_*` contract, creates one page per contract mod,
  and calls `SkyrimMod_Init(page)`.
- External mods may also expose harmless SKSE exports so SKSE accepts them when it scans the normal
  plugin folder. HagUI still owns page creation.

## Build

- Build/deploy the loader/UI:
  `HagUI\build.ps1`
- Build/deploy all external mods:
  `mods\build_mods.ps1`
- Build/deploy one external mod:
  `mods\build_mods.ps1 -Only HagGeneral`

Build scripts deploy to Mod Organizer 2 by default and call `scripts\auto-git-commit.cjs` after a
successful build unless `-NoCommit` is passed.

## Conventions

- Manual SKSE/Ghidra approach: no Address Library, no CommonLibSSE-NG.
- One repo owns loader, shared ABI, tools, and feature mods.
- `HagCharacter` has been removed; `HagGeneral` is the first external mod under `mods/`.
