#include "PCH.h"
#include "UI/HagMenu.h"
#include "UI/OptionRender.h"
#include "api/HagApi.h"
#include "GameState.h"
#include "Log.h"
#include "Offsets.h"
#include "Hooking.h"

// HagUIMenu wiring. Modelled byte-for-byte on the RE'd BarterMenu creator
// (docs/UI-RE.md): allocate -> set a 9-slot IMenu vtable -> LoadMovie -> flags.
// Create() and the vtable are NOT exercised until the menu is shown; Register()
// is the only part run so far (safe — it just inserts into the menu map).
namespace hag::ui {

namespace {
    bool g_open = false;

    using AllocFn     = void* (*)(void* self, std::size_t size, std::size_t align);
    using FreeFn      = void  (*)(void* self, void* ptr);
    using LoadMovieFn = bool  (*)(void* sfMgr, void* menu, void* viewOut, const char* name, int scaleMode, int unk);
    using RegisterFn  = void  (*)(void* registry, const char* name, void* creator);

    void* Allocator()  { return *reinterpret_cast<void**>(offsets::FromRVA(offsets::kMenuAllocator_ptr)); }

    // --- our IMenu vtable: overrides + reused generic game bases (see imenu_vtable) ---
    void* HagDtor(void* self, unsigned int flags);
    void  HagRegisterFuncs(void* self, void* movieView);
    unsigned int HagProcessMessage(void* self, void* msg);
    void  HagTick(void* self);   // AdvanceMovie(slot 6): pump debounce + build option pages, then advance

    void** Vtable() {
        static void* vt[9] = {};
        if (!vt[0]) {
            vt[0] = reinterpret_cast<void*>(&HagDtor);                       // ~Menu
            vt[1] = reinterpret_cast<void*>(&HagRegisterFuncs);              // RegisterFuncs
            vt[2] = reinterpret_cast<void*>(offsets::FromRVA(offsets::kIMenuBase_2));
            vt[3] = reinterpret_cast<void*>(offsets::FromRVA(offsets::kIMenuBase_3));
            vt[4] = reinterpret_cast<void*>(&HagProcessMessage);            // ProcessMessage
            vt[5] = reinterpret_cast<void*>(offsets::FromRVA(offsets::kIMenuBase_5));
            vt[6] = reinterpret_cast<void*>(&HagTick);                      // AdvanceMovie -> our tick
            vt[7] = reinterpret_cast<void*>(offsets::FromRVA(offsets::kIMenuBase_7));
            vt[8] = reinterpret_cast<void*>(offsets::FromRVA(offsets::kIMenuBase_8));
        }
        return vt;
    }

    // AdvanceMovie (IMenu vtable slot 6), run every frame HagUI is up. We pump the
    // option debounce timers and (once per movie) build the registered option pages,
    // then forward to the game's generic tick (kIMenuBase_6, 0x5739C0) so the SWF
    // still advances/renders. That generic tick is NOT hooked by us (the main-menu
    // entry now hooks the list-build fn instead), so forwarding here is a plain call.
    void HagTick(void* self) {
        hag::api::HagUI::Get().PumpDebounce(::GetTickCount64());
        void* view = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + offsets::menu_layout::kMovieView);
        if (view) { OptionRender::BuildIfNeeded(view); OptionRender::UpdateLive(view); }
        reinterpret_cast<void (*)(void*)>(offsets::FromRVA(offsets::kIMenuBase_6))(self);
    }

    void* HagDtor(void* self, unsigned int flags) {
        HAG_INFO("HagUIMenu::~ flags={:#x}", flags);
        g_open = false;
        if (flags & 1) {
            void* a = Allocator();
            auto** avt = *reinterpret_cast<void***>(a);
            reinterpret_cast<FreeFn>(avt[0x60 / 8])(a, self);
        }
        return self;
    }

    // HagRegisterFuncs (defined below, needs the MakeStrFn/RegCBFn typedefs) binds the
    // "CloseHagUI" GameDelegate callback onto HagUI's OWN movie.

    // Visuals are authored entirely in HagUI.swf (assets/HagUI_Root.as builds the black+gold
    // Welcome from AS2: gradients, embedded Skyrim fonts, rounded gold-hairline panels). C++ no
    // longer draws into _root.
    using ProcMsgFn = unsigned int (*)(void* self, void* msg);
    unsigned int HagProcessMessage(void* self, void* msg) {
        const unsigned int type = *reinterpret_cast<unsigned int*>(reinterpret_cast<char*>(msg) + 8);
        if (type == offsets::kMsg_Show) {
            HAG_INFO("HagUIMenu::ProcessMessage kShow (SWF renders the Welcome)");
            g_open = true;
            game_state::SetGameRunning(false, "HagUI shown");
            return 0;
        }
        // Delegate everything else to the base IMenu ProcessMessage. Critically, kUserEvent(6)
        // input is forwarded there to the movie's HandleEvent (movie->vtable[0x168]) -> the SWF's
        // CLIK Key/Mouse listeners fire -> handleInput/onMouseDown -> GameDelegate.call("CloseHagUI").
        // Without this, the movie never sees keyboard/mouse and nothing can close the menu.
        return reinterpret_cast<ProcMsgFn>(offsets::FromRVA(offsets::kIMenuBase_ProcessMsg))(self, msg);
    }

    // The "HagUI" entry now opens our panel directly from our own trampolined AS dispatch handlers
    // (GfxInject.cpp owns StartMenu.onMainButtonPress / SystemPage.onCategoryButtonPress and calls
    // HagMenu::Open() for our row), so we no longer register an "OpenHagUI" GameDelegate callback on
    // the host menus. Only CloseHagUI (below) is still registered — on HagUI's OWN movie.
    using MakeStrFn  = void (*)(void* gfxStringOut, const char* str);
    using RegCBFn    = void (*)(void* movie, void* gfxName, void* handler);  // movie->vtable[1]

    // --- close-trigger: HagUI's own SWF calls gfx.io.GameDelegate.call("CloseHagUI") on ESC/click ---
    // We register that callback on HAGUI's own movie, byte-for-byte like OpenHagUI on the main-menu
    // movie. Hag_CloseHandler posts kHide. (The CreditsMenu creator FUN_140913bb0 likewise calls its
    // own RegisterFuncs FUN_1409133b0(menu) explicitly at the end of Create — we mirror that below.)
    void Hag_CloseHandler(void* /*params*/) {
        HAG_INFO("CloseHagUI callback -> closing HagUIMenu");
        HagMenu::Close();   // posts kHide(=3) via UIMessageQueue (already working)
    }

    void HagRegisterFuncs(void* /*self*/, void* movieView) {
        if (!movieView) { HAG_WARN("HagUI RegisterFuncs: null movie - cannot add CloseHagUI"); return; }
        std::uint64_t name = 0;
        reinterpret_cast<MakeStrFn>(offsets::FromRVA(offsets::kGFx_MakeString))(&name, "CloseHagUI");
        void** mvt = *reinterpret_cast<void***>(movieView);
        reinterpret_cast<RegCBFn>(mvt[offsets::kGFxMovie_RegisterCB])(   // movie->vtable[1]
            movieView, &name, reinterpret_cast<void*>(&Hag_CloseHandler));
        if (name) {  // drop our ref to the managed GFx string (atomic refcount at (ptr&~3)+8), same as OpenHagUI
            auto* rc = reinterpret_cast<volatile long*>((name & ~3ull) + 8);
            if (_InterlockedDecrement(rc) == 0) {
                void* a = Allocator();
                reinterpret_cast<FreeFn>((*reinterpret_cast<void***>(a))[0x60 / 8])(
                    a, reinterpret_cast<void*>(name & ~3ull));
            }
        }
        HAG_INFO("HagUIMenu: registered 'CloseHagUI' GameDelegate callback (movie={})", movieView);
    }

    // --- BSInputEventUser input handler (+0x30): official close on Cancel, mirroring Credits ---
    void* HagInputDtor(void* self, unsigned int) { return self; }
    void HagInputHandler(void* /*self*/, void* event) {
        const int t = *reinterpret_cast<int*>(reinterpret_cast<char*>(event) + 0x30);
        HAG_INFO("HagUIMenu input event type={}", t);
        if (t == 2) HagMenu::Close();  // 2 == Cancel/back (same as Credits' handler)
    }
    void** InputVtable() {
        static void* vt[2] = {};
        if (!vt[0]) {
            vt[0] = reinterpret_cast<void*>(&HagInputDtor);
            vt[1] = reinterpret_cast<void*>(&HagInputHandler);
        }
        return vt;
    }
}  // namespace

void* HagMenu::Create() {
    HAG_INFO("HagUIMenu::Create");
    void* a = Allocator();
    if (!a) { HAG_ERR("HagUIMenu::Create - no allocator"); return nullptr; }
    auto** avt = *reinterpret_cast<void***>(a);
    void* menu = reinterpret_cast<AllocFn>(avt[0x50 / 8])(a, 0x40, 0);
    if (!menu) { HAG_ERR("HagUIMenu::Create - alloc failed"); return nullptr; }
    std::memset(menu, 0, 0x40);
    // IMenu/BSInputEventUser base ctor FIRST (sets up + registers the input sink) - every real creator does this.
    reinterpret_cast<void (*)(void*)>(offsets::FromRVA(offsets::kIMenu_baseCtor))(menu);
    *reinterpret_cast<void***>(menu) = Vtable();                                   // override +0x00 IMenu vtable
    *reinterpret_cast<void***>(reinterpret_cast<char*>(menu) + 0x30) = InputVtable();  // override +0x30 BSInputEventUser
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(menu) + 0x38) = 1;       // (Credits sets this)

    void* sfMgr = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kBSScaleformManager_ptr));
    auto  loadMovie = reinterpret_cast<LoadMovieFn>(offsets::FromRVA(offsets::kScaleform_LoadMovie));
    void* viewSlot = reinterpret_cast<char*>(menu) + offsets::menu_layout::kMovieView;  // +0x10
    const bool ok = loadMovie(sfMgr, menu, viewSlot, "HagUI", 1, 0);
    // Renders over the Main Menu: depth 10 (> MainMenu's 9).
    *reinterpret_cast<std::uint8_t*>(reinterpret_cast<char*>(menu) + 0x18)  = 10;  // depth/priority
    // MENU_FLAGS (+0x1c). Traced the ACTUAL engine decision (not inferred from flag values):
    // FUN_140fa5e70 (runs on every menu change) takes the topmost menu with depth<0xb and, iff
    // (topmost->flags & 0x4) != 0, posts AddMessage("Cursor Menu", kShow) -> the cursor appears.
    // So bit 0x4 (kUsesCursor) is THE cursor flag. HagUI is the topmost menu (depth 10), so it must
    // carry 0x4. Value 0x5 = kPausesGame(0x1) [Credits sets this and gets keyboard+mouse input] |
    // kUsesCursor(0x4). Kept minimal: the earlier 0x581 lacked 0x4 (no cursor) and its extra bits
    // (0x80/0x100/0x400) only bump unrelated UI counters and risk disturbing input.
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(menu) + 0x1c) = 0x5;
    // +0x20 = input-context ID (NOT flags). FUN_140fa3fa0: if (menu[+0x20] != 0x13) push that context
    // via FUN_140cd5450. 1 = menu mode (Credits uses 1 and receives ESC/clicks). Keep 1.
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(menu) + 0x20) = 1;
    HAG_INFO("HagUIMenu::Create - LoadMovie('HagUI')={} menu={} depth=10 flags=0x5(cursor) ctx=1", ok, menu);

    // Bind our AS-facing callbacks now that the movie is loaded (mirrors the CreditsMenu creator's
    // trailing FUN_1409133b0(menu) call). Reads the GFxMovieView* LoadMovie wrote into menu+0x10.
    if (ok) {
        void* view = *reinterpret_cast<void**>(reinterpret_cast<char*>(menu) + offsets::menu_layout::kMovieView);
        HagRegisterFuncs(menu, view);  // register "CloseHagUI" on our own movie
    } else {
        HAG_WARN("HagUIMenu::Create - LoadMovie failed; skipping RegisterFuncs");
    }
    return menu;
}

void HagMenu::Open() {
    game_state::SetGameRunning(false, "HagUI open requested");
    void* queue = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kUIMessageQueue_ptr));
    if (!queue) { HAG_WARN("HagUIMenu::Open - no UIMessageQueue"); return; }

    struct BSFixedString { const char* data = nullptr; } name;  // 8-byte interned-string handle
    reinterpret_cast<void (*)(void*, const char*)>(offsets::FromRVA(offsets::kBSFixedString_ctor))(&name, kName);

    using AddMsgFn = void (*)(void* queue, void* name, std::uint32_t type, void* data);
    reinterpret_cast<AddMsgFn>(offsets::FromRVA(offsets::kUIMessageQueue_AddMsg))(
        queue, &name, offsets::kMsg_Show, nullptr);

    reinterpret_cast<void (*)(void*)>(offsets::FromRVA(offsets::kBSFixedString_dtor))(&name);
    HAG_INFO("HagUIMenu::Open - posted kShow");
}

void HagMenu::Close() {
    void* queue = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kUIMessageQueue_ptr));
    if (!queue) return;
    struct BSFixedString { const char* data = nullptr; } name;
    reinterpret_cast<void (*)(void*, const char*)>(offsets::FromRVA(offsets::kBSFixedString_ctor))(&name, kName);
    reinterpret_cast<void (*)(void*, void*, std::uint32_t, void*)>(offsets::FromRVA(offsets::kUIMessageQueue_AddMsg))(
        queue, &name, offsets::kMsg_Hide, nullptr);
    reinterpret_cast<void (*)(void*)>(offsets::FromRVA(offsets::kBSFixedString_dtor))(&name);
    HAG_INFO("HagUIMenu::Close - posted kHide");
}

bool HagMenu::IsOpen() {
    return g_open;
}

void HagMenu::Register() {
    void* registry = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kUI_Registry_ptr));
    if (!registry) { HAG_WARN("HagUIMenu::Register - UI registry null (too early?)"); return; }
    reinterpret_cast<RegisterFn>(offsets::FromRVA(offsets::kUI_Register))(
        registry, kName, reinterpret_cast<void*>(&HagMenu::Create));
    HAG_INFO("HagUIMenu registered via UI::Register (registry={})", registry);
}

void HagMenu::InstallTrigger() {
    // Install the "HagUI" entry into BOTH the Main Menu and the in-game System menu by hooking each
    // menu's own list-build path and trampolining its AS click-dispatch method (no SWF edit, SkyUI-safe).
    // See GfxInject.cpp for the full design.
    InstallSystemInject();
}

}  // namespace hag::ui
