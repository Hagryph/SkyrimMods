# HagUI

A button in Skyrim's in-game Options that opens our own **Scaleform** UI hub for all of Hagryph's
mods. Black + gold theme. Built the manual way: **Ghidra-found addresses, MinHook, no Address Library**.

- Runtime: **SkyrimSE.exe 1.6.1170** (AE)
- Stack: SKSE bare loader -> MinHook -> the game's native Scaleform GFx UI (no external overlay)
- Reverse-engineering plan + address table: [`docs/UI-RE.md`](docs/UI-RE.md)

## Build

```
cmake --preset vs2022
cmake --build build --config Release
```

Output: `build/Release/HagUI.dll`.

**Build + deploy as a Mod Organizer 2 mod:** `./build.ps1` — builds, then stages into
`…\ModOrganizer\Skyrim Special Edition\mods\HagUI\` (`SKSE\Plugins\HagUI.dll`, `Interface\HagUI.swf`).
`./deploy.ps1` deploys without rebuilding.

## Roadmap

- [x] **P0** loader scaffold (SKSE version data, logging, MinHook, offsets stub)
- [ ] **P1** bindings - fill `src/Offsets.h` from `ghidra/ui_dump.txt`
- [ ] **P2** register + show the `HagUIMenu` Scaleform menu (hotkey first)
- [ ] **P3** draw the golden/black UI via GFx
- [ ] **P4** forwarder hook on a game UI function
- [ ] **P5** inject the button into the in-game Options

## Layout

| Path | What |
|------|------|
| `src/Plugin.cpp` | SKSE entry -> `hag::Plugin` |
| `src/SKSE_Min.h` | hand-written SKSE plugin ABI |
| `src/Hooking.*` | MinHook trampoline wrapper |
| `src/Offsets.h` | hand-found addresses (RVA off image base `0x140000000`) |
| `src/UI/HagMenu.*` | the Scaleform menu |
| `docs/UI-RE.md` | how Skyrim's UI works + how we hook in |
