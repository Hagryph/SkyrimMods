# HagUI — Skyrim SE UI reverse-engineering & hook plan

Target runtime: **SkyrimSE.exe v1.6.1170** (AE). Approach: **manual** — addresses from Ghidra,
hooks via MinHook, UI built with the **game's own Scaleform GFx** system (no Address Library,
no CommonLibSSE, no external overlay). Image base in the PE is `0x140000000`; a Ghidra file
address `0x14xxxxxxx` maps to runtime as `GetModuleHandle(NULL) + (fileAddr - 0x140000000)`.

---

## 1. How Skyrim renders its UI

Skyrim's UI is **Autodesk Scaleform GFx** — Flash (ActionScript 2) movies driven by the engine:

```
  .swf assets  (Data/Interface/*.swf)        <- the visual layer (Flash)
        |  loaded into
  GFxMovieView / GFxMovieDef                  <- one running movie per menu
        |  owned by
  IMenu  subclass (InventoryMenu, ...)        <- native C++ wrapper per menu
        |  registered in / driven by
  UI (a.k.a. MenuManager) singleton           <- name -> menu registry + render/update
        |  opened & closed via
  UIMessageQueue  (BSUIMessageData)           <- "show MapMenu", "hide MapMenu", ...
        |  movies loaded through
  BSScaleformManager / BSScaleformMovieLoader <- LoadMovie / LoadMovieEx
        |  native <-> ActionScript bridge
  GFxValue / GFxFunctionHandler::Invoke       <- call AS from C++, register C++ callbacks
```

Each frame the engine ticks the `UI` singleton, which updates/renders every **open** menu's
`GFxMovieView`. So to put our own UI "inside" Skyrim we register a menu, give it a movie, and
either load a custom `.swf` or build the display list at runtime through GFx calls.

## 2. Native objects we need (fields/vtable slots to confirm in Ghidra)

| Object | Why we need it | What to extract |
|--------|----------------|-----------------|
| `UI` / `MenuManager` singleton | the menu registry + per-frame driver | singleton accessor address; `menuMap` (BSTHashMap name→entry); `IsMenuOpen` |
| `UI::Register(name, creator)` | add our menu so the engine knows "HagUIMenu" | function address + ABI (BSFixedString, fn ptr) |
| `IMenu` vtable | our menu subclass must match the engine's vtable | vtable layout: `Ctor`, `Dtor`, `ProcessMessage`, `AdvanceMovie`, `Render`, `RefreshPlatform`... |
| `MENU_FLAGS` | behaviour: pause game, cursor, modal, depth | flag bit meanings |
| `UIMessageQueue::AddMessage` | open/close our menu by name | function address + `UI_MESSAGE_TYPE` enum (kShow/kHide) |
| `BSScaleformManager::LoadMovieEx` | load our `.swf` into the menu's `GFxMovieView` | function address + ABI |
| `GFxValue::Invoke` / `GFxFunctionHandler` | call AS + receive AS→C++ callbacks (button clicks) | function addresses + GFxValue layout |
| `GFxMovieView` | the movie root we draw into / invoke on | vtable slots `Invoke`, `CreateObject`, `SetVariable` |

The `DumpUI.java` output (`ghidra/ui_dump.txt`) gives us:
- every UI string and the functions referencing it,
- the **menu-registrar function** (the one function that references *all* the `*Menu*` name
  strings — that's where `UI::Register` is called once per menu; the cleanest ABI sample),
- recovered RTTI class names (`BSScaleformManager`, `GFxMovieView`, `IMenu`, ...),
- symbols for `LoadMovie*`, `Register`, `GFxValue`, `Invoke`, `UIMessage*`.

## 3. Menu lifecycle (what we replicate)

1. **Register** (once, on plugin load): `UI::Register("HagUIMenu", HagUIMenu::Create)`.
2. **Open**: push a show message — `UIMessageQueue::AddMessage("HagUIMenu", kShow)`.
3. **Create**: engine calls our `Create()` → returns a `HagUIMenu : IMenu`; its ctor calls
   `BSScaleformManager::LoadMovieEx(this, view, "HagUI", ...)` to bind our movie.
4. **Drive**: engine ticks `AdvanceMovie` + `Render`; AS button clicks arrive in our
   `GFxFunctionHandler` callbacks; we react in C++.
5. **Close**: `AddMessage("HagUIMenu", kHide)` → engine destroys the menu.

## 4. How SkyUI does the "button in Options" (the part we mirror)

SkyUI ships replacement `.swf`s and an SKSE plugin. Its **Mod Configuration Menu** is a custom
menu; the **"MOD CONFIGURATION" button** is injected into the **Journal/System (pause) menu** by
SkyUI's modified `journalmenu`/`configmanager` SWF talking to its plugin. Replicating faithfully
means either (a) editing/our-own Journal SWF, or (b) hooking the Journal menu's native option
handling. For the **test-run**, HagUI opens via a hotkey first (cheap, proves the whole pipeline),
then we add the in-Options button as step 2 once the menu pipeline is confirmed working.

## 5. HagUI build phases

- **P0 — Loader**: bare SKSE plugin (`SKSEPlugin_Version` data) → DLL loads, logs, gets module base.
- **P1 — Bindings**: from `ui_dump.txt`, wrap the addresses above as typed function pointers
  (`base + RVA`). Verify each by logging a known call (e.g. `IsMenuOpen("MapMenu")`).
- **P2 — Register + show**: register `HagUIMenu`, open via hotkey. Even an empty movie proves it.
- **P3 — Render golden/black UI**: draw the panel (two options in §6).
- **P4 — Forwarder hook**: MinHook a game UI function (e.g. the menu-process or journal-options
  builder), do our thing, then **call the original** — demonstrates hook + forwarder.
- **P5 — Options button**: inject the "HagUI" entry into the in-game options.

## 6. Rendering the golden/black UI — two paths

- **A. Custom `.swf`** (most SkyUI-faithful): author a Flash movie themed golden/black, load via
  `LoadMovieEx`. Pixel-perfect, but needs an SWF toolchain (JPEXS/FFDec or an AS2 compiler).
- **B. Native GFx drawing** (no SWF authoring): get a writable movie root and build the display
  list from C++ via `GFxValue` — `CreateEmptyMovieClip`, `Graphics.beginFill/lineStyle/drawRect`,
  `createTextField` — to paint golden/black panels, buttons, text. "Replicate UI functions to
  build our own UI" literally.

**Chosen for the test-run: B**, falling back to a minimal stub `.swf` only if a writable root
can't be obtained without one. Decision finalised after the dump confirms `GFxValue::Invoke`/
movie-root access.

## 7. Address table — recovered from the DECRYPTED 1.6.1170 (RVAs off image base 0x140000000)

> The retail exe is **SteamStub-encrypted**; analysis was done on a Steamless-unpacked copy
> (`C:\dev\re\SkyrimSE.exe.unpacked.exe`). RVAs map to the live game unchanged. Raw findings in
> `ghidra/ui_xref.txt` (xrefs), `ghidra/ui_decomp.txt` (pseudocode), `ghidra/ui_refs.txt` (RTTI).

| Symbol | RVA | Status | Notes |
|--------|-----|--------|-------|
| `UI` ctor (menu-name table) | `0xFB7E10` | ✅ | interns 47 menu BSFixedStrings; sets singleton global |
| `UI*` singleton global | `0x20F8958` | ✅ | `*(UI**)(base+rva)` |
| `BSFixedString::ctor` | `0xCEC5D0` | ✅ | `(dst, const char*)` |
| `BSScaleformManager::LoadMovie` | `0xFB0110` | ✅ | `(this, IMenu*, GFxMovieView**, name, flags)`; builds `Interface/<name>.swf` |
| `GFxMovieView::Invoke(path)` | `0xFBFB10` | ✅ | invokes `_root.x` AS |
| `GFx` movie file loader | `0x1034E30` | ✅ | returns movie def |
| `BSScaleformFileOpener::Open` | `0xFB4490` | ✅ | BSResource/BSA-backed (the BSA chain) |
| `GFxLoader::Read` | `0x10323F0` | ✅ | SWF/GFX parse |
| `GFxValue::ObjectInterface` API | `0x10C3AB0` | ✅ | CreateEmptyMovieClip/AttachMovie/SetText/Invoke… |
| `UI::Register(reg, name, creator)` | `0xFA5480` | ✅ | name is `const char*`; creator is `IMenu*(*)()` |
| UI registry getter | `0xFA32F0` | ✅ | caches global `0x20F6A00` |
| `BSScaleformManager*` global | `0x35F11C8` | ✅ | passed to LoadMovie |
| menu allocator global | `0x3292490` | ✅ | `Allocate` at vtable `+0x50` |
| `IMenu` vtable (BarterMenu) | `0x18F0CE0` | ✅ | **9 slots**; template to clone |
| `UIMessageQueue::AddMessage` | `TBD` | ⏳ | open/close menu (next) |
| Options-menu inject point | `TBD` | ⏳ | trampoline + call original (next) |

**Register a custom menu (the plan for HagUIMenu):**
```c
auto reg = ((Registry*(*)(void*))FromRVA(0xFA32F0))(&param);     // or read 0x20F6A00
((void(*)(void*, const char*, IMenu*(*)()))FromRVA(0xFA5480))(reg, "HagUIMenu", &HagUIMenu::Create);
// Create(): allocate -> set 9-slot IMenu vtable (clone 0x18F0CE0) -> LoadMovie(BSScaleformMgr, this, &this[0x10], "HagUI", 2, 0) -> flags
```

`GFxMovieView` vtable slots (from LoadMovie): +0x50 Invoke, +0x88 GetVariable, +0xC0 CreateView,
+0xD8 SetScaleMode, +0xF8 GetVisibleRect, +0x118 SetViewport, +0x128 Display.

## 8. Theme — black + gold (from Manga List / LoL Game Helper / GrepolisMod)

Single gold accent on near-black layered panels; uppercase letter-spaced labels; gold-tinted
borders; gradient + soft-glow active states. Scaleform uses `0xRRGGBB` ints + an alpha 0-100.

| Token | Hex | Use |
|-------|-----|-----|
| bg-0 | `#0A0A0C` | deepest background |
| bg-1 | `#121013` | window background |
| panel | `#1A1712` (~92% a) | card / panel fill |
| panel-2 | `#231E16` | nested panel / input |
| panel-hover | `#2E271B` | hover fill |
| border | `#E0B34A` @ 14% | hairline borders |
| border-strong | `#E0B34A` @ 42% | active / focused borders |
| **accent (GOLD)** | `#E0B34A` | primary accent, titles, active |
| accent-dim | `#B8862F` | gradient bottom, pressed |
| accent-glow | `#E0B34A` @ 50% | glows / focus ring |
| text | `#ECE6DA` | body text |
| text-dim | `#9C9486` | secondary text / labels |
| text-faint | `#6B6456` | captions |
| win / loss | `#3FB27F` / `#E0556B` | success / error |

Buttons: gradient `accent@18% → accent@6%`, 1px `border-strong`, gold text, uppercase +1px
letter-spacing, glow on hover. Radius 10px (6px small).

## 9. Build / deploy

- CMake + vcpkg (`minhook`, `spdlog`), MSVC x64 → `HagUI.dll`.
- `deploy.ps1` → `…/Skyrim Special Edition/Data/SKSE/Plugins/HagUI.dll` (+ `Data/Interface/HagUI.swf` if path A).

## 10. Runtime, SkyUI-compatible menu-entry injection — RE findings & plan

Motivation: shipping a modified `startmenu.swf`/`quest_journal.swf` **file** conflicts with any UI
mod that replaces the same file (MO2 load order → one winner, no merge). Injecting the entry into
the **live movie at runtime** works over *whatever* SWF loaded (vanilla, SkyUI, an overlay) → no
file conflict. Chosen approach (2026-07-01).

**Confirmed from `ghidra/ui_decomp5.txt` / `ui_decomp6.txt`:**
- `MainMenu::RegisterFuncs` (0x941CC0) & `JournalMenu::RegisterFuncs` (0x994C10) are flat lists: per
  callback `FUN_140fbb150(&name,"Name")` (GFx MakeString) → `param_2->vtable[1](param_2,&name,handler)`
  → release the interned string (atomic dec refcount at `(name&~3)+8`; if 0, free via allocator
  `vtable+0x60`). `param_2` is the shared **GameDelegate (FxDelegate)**. This is exactly what
  `RegisterOpenHagUI` already does.
- `JournalMenu::RegisterFuncs` registers RequestPlayerInfo/RememberCurrentTabIndex/PlaySound/
  CloseMenu/ShouldShowMod/myLog on that one delegate. `SystemPage.as` (quest_journal) calls those
  **and** the save/load ones through the SAME `gfx.io.GameDelegate`. ⇒ **our `OpenHagUI`, registered
  on the journal delegate, is already reachable from the System page's AS.** The native click
  *endpoint* exists; no SWF edit is needed for the click target.
- **The gap:** the compiled `SystemPage.as` builds `entryList` (onLoad) and dispatches clicks by
  positional `event.index` (`onCategoryButtonPress`). RegisterFuncs only supplies callbacks — it does
  not add our row, nor make the AS call `OpenHagUI` for it.
- **C++→AS surface exists:** `GFxValue::ObjectInterface` (HasMember/GetMember/SetMember/Invoke/
  PushBack/GetArraySize/SetElement/AttachMovie/CreateEmptyMovieClip…) — method roster confirmed via
  the AMP name table `FUN_1410c3ab0` (0x10C3AB0). `GFxMovieView::Invoke` = 0xFBFB10 → inner
  `FUN_141021aa0`/`FUN_141009e10`, which marshal `GFxValue`s through the AS env (`movie+0x58`, interp
  via `vtable+0xe0`, string intern `FUN_140fc87b0`, value build `FUN_140fcd600`). GFxValue **type
  codes** (from the `FUN_140fcd600` switch): 2=bool, 3/4=number, 5=string, 6/7=object/displayobj.

**Plan (all runtime; no SWF file shipped):**
1. **Trigger/timing** — inject *after* `SystemPage.onLoad` builds `entryList` (RegisterFuncs is too
   early). Either hook a per-frame/advance and inject once `GetVariable("…entryList")` resolves, or
   register a native GameDelegate callback the page calls post-build and inject there.
2. **Insert row** — `GetVariable` the `CategoryList.entryList` GFxValue; build an object GFxValue
   (`SetMember "text"="HagUI"`, `"hagui"=true`); insert via ObjectInterface `PushBack`/element ops.
3. **Wire click** — cleanest: wrap `onCategoryButtonPress` with a native function (GetMember original
   → SetMember a native wrapper that routes `entry.hagui → OpenHagUI`, fixes the shifted index, else
   calls the saved original). Alt: add a native-scope `itemPress` listener via the delegate.

**Remaining RE before coding:** `GFxValue` struct layout; ObjectInterface method addresses
(SetMember/GetMember/PushBack/GetArraySize); `CreateFunction` (how `movie->vtable[1]` builds the
native fn value); FxDelegate/CLIK `dispatchEvent`. Then build + **hotload-iterate** (close game to
rebuild the DLL; load a save; open System). The current modified-SWF build stays as the working
fallback until the runtime path is validated in-game.

### 10a. VALIDATED live (2026-07-01) via HagIPC call/read/write into the running game

Phase 1 (row appears via pure runtime injection) is **confirmed in-game**: a `{text:"HagUI"}` row
was appended to the open System menu's live list from outside the process, no SWF touched, no crash.

**GFxValue** (size 0x18): `+0x00 ObjectInterface*` · `+0x08 Type` (VT_Object=6, VT_String=4,
VT_DisplayObject=8; `|0x40`=managed) · `+0x10 value/handle`. **Always zero the out-buffer before
Create*/GetVariable** — they release the old value first and choke on garbage.

**GFxMovieView vtable slots** (byte offsets; call `(*(*movie+slot))(movie,…)`), all confirmed by
decompiling the slot bodies (`ui_decompA/B.txt`) + live calls:
| slot | method | ABI |
|------|--------|-----|
| +0x50 | IsAvailable | `(movie, const char* path)` → bool |
| +0x58 | CreateString | `(movie, GFxValue* out, const char* cstr)` |
| +0x68 | CreateObject | `(movie, GFxValue* out, const char* className=0, GFxValue* args=0, uint nargs=0)` |
| +0x70 | CreateArray | `(movie, GFxValue* out)` |
| +0x78 | CreateFunction | `(movie, GFxValue* out, GFxFunctionHandler* fh, void* userData)` ← native click |
| +0x80 | SetVariable | `(movie, const char* path, GFxValue* val, int setType)`; setType 0 ⇒ create **leaf on existing parent** (NOT deep-vivify) |
| +0x88 | GetVariable | `(movie, GFxValue* out, const char* path)` |
| +0xa0 | GetVariableArraySize | `(movie, const char* path)` → int |
| +0xb0 | Invoke | `(movie, path, GFxValue* resultOut/0, GFxValue* args/0)` |
Standalone Invoke = `FUN_140fbfb10(movie, path, args/0)` (RVA 0xFBFB10) — proven no-arg form.

**Open-menu stack** (to get a menu's live movie with no instance global): `mgr = *(0x20F6A00)`;
array `*(mgr+0x110)`, count `*(mgr+0x120)` (int), entries are `IMenu*`; per IMenu `+0x00`=vtable,
`+0x10`=GFxMovieView, `+0x18`=depth(u8), `+0x1c`=flags. Match `JournalMenu` by vtable RVA `0x190AF38`
(MainMenu `0x18FC980`). Scratch memory: game menu-allocator `*(0x3292490)`, `Allocate` at its
`vtable+0x50` `(alloc, size, align)`.

**entryList AS path** (System page): `_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc.CategoryList_mc.List_mc.entryList`
(== live `EntriesA`). Refresh via Invoke `…List_mc.InvalidateData` (no args).

**Injection recipe (validated):** GetVariableArraySize→N · zero+CreateObject→obj ·
SetVariable(`…entryList.N`, obj, 0) · CreateString "HagUI"→str · SetVariable(`…entryList.N.text`,
str, 0) · Invoke `…InvalidateData`. Result: arraySize 10→11, row visible. All from
`scratchpad/inject_journal.ps1`.

**Phase 2 (click) — native only.** Dispatch is positional; our appended index hits vanilla
`onCategoryButtonPress`'s `default` (harmless), so the click needs a real `GFxFunctionHandler`
attached as the list's `itemPress` listener (CreateFunction +0x78). That persistent C++ object must
live in HagUI.dll — build it there (native, main-thread, correct timing), replacing the SWF edit.
