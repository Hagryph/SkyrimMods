# HagLoader

Main Skyrim SKSE loader. It contains the **HagUI** Scaleform hub, external mod discovery, the
cross-mod UI API, and loader-internal services such as queued console/script execution.

- Runtime: **SkyrimSE.exe 1.6.1170** (AE)
- Stack: SKSE bare loader -> MinHook -> the game's native Scaleform GFx UI
- UI notes: [`docs/UI-RE.md`](docs/UI-RE.md)

## Build

```powershell
cmake --preset vs2022
cmake --build build --config Release
```

Output: `build/Release/HagLoader.dll`.

Build + deploy as a Mod Organizer 2 mod:

```powershell
.\build.ps1
```

Deploy target:

`...\ModOrganizer\Skyrim Special Edition\mods\HagLoader\SKSE\Plugins\HagLoader.dll`

The UI movie remains `Interface\HagUI.swf`.

## Layout

| Path | What |
|------|------|
| `src/Plugin.cpp` | SKSE entry -> `hag::Plugin` |
| `src/SKSE_Min.h` | hand-written SKSE plugin ABI |
| `src/Hooking.*` | MinHook trampoline wrapper |
| `src/ConsoleExec.*` | internal console/Papyrus script execution through Skyrim's own `CompileAndRun` path |
| `src/ConsoleQueue.*` | internal SKSE task-queued console command runner |
| `src/LoaderApiExport.cpp` | exports the narrow queue-only `HagLoader_GetAPI`, with optional async result callback |
| `src/ModManager.*` | discovers external mods and passes each one its HagUI page |
| `src/Offsets.h` | hand-found addresses (RVA off image base `0x140000000`) |
| `src/UI/HagMenu.*` | the Scaleform menu |
| `src/api/HagApi*` | HagUI option-page model and exported `HagUI_GetAPI` |
