#include "PCH.h"
#include "UI/HagMenu.h"
#include "UI/OptionRender.h"
#include "GameState.h"
#include "Log.h"
#include "Offsets.h"
#include "Hooking.h"

#include <cstdio>
#include <cstring>

// Runtime, SkyUI-compatible "HagUI" entry in BOTH the Main Menu and the in-game System (pause) menu.
//
// No SWF file is shipped or edited. We drive the LIVE movies through the game's own Scaleform GFx API.
// The KEY design (2026-07-01, rev 2): rather than splice a row and then patch positional dispatch with
// an index-rewrite hack, we HOOK THE ACTIONSCRIPT ITSELF. Each menu's list dispatches item clicks
// through one AS handler method (StartMenu.onMainButtonPress / SystemPage.onCategoryButtonPress). We
// TRAMPOLINE that method: replace it on the AS object with a native GFxFunctionHandler, keep the
// original stashed under a sibling member, and forward every non-HagUI press to the original via
// GFxMovieView::Invoke(args) (movie vtable +0xB8). So the game's own dispatch runs for vanilla rows and
// we own only our row -- no shared-event mutation, no listener-order dependency, mouse == keyboard.
//   * Main menu dispatches by CARRIED entry.index -> our row {index:90} needs no positional fix-up.
//   * System menu dispatches by POSITIONAL event.index -> we correct it by exactly our one inserted row
//     before forwarding (the game sees the pre-insert position it expects).
// Every offset/ABI here was recovered in Ghidra and validated live via HagIPC (docs/UI-RE.md §10/§11).
namespace hag::ui {
namespace {

using namespace hag::offsets;

// Scaleform GFxValue (0x18). MUST be zeroed before Create*/GetVariable: those release the prior value
// first, so uninitialised bytes make them free garbage (observed live). type at +0x08 (0 == undefined).
struct GFxValue { void* objIface; std::uint32_t type; std::uint32_t typeHi; std::uint64_t value; };
static_assert(sizeof(GFxValue) == 0x18, "GFxValue must be 0x18");

// GFxFunctionHandler::Params (from FUN_140fac280): +0x00 ret, +0x08 movie, +0x10 self, +0x18 argsThis,
// +0x20 args (GFxValue* to the arg array), +0x28 argc(int), +0x30 userData. args[0] is the first AS arg.
struct FnParams { GFxValue* ret; void* movie; GFxValue* self; GFxValue* argsThis; GFxValue* args; int argc; int pad; void* userData; };

// --- GFxMovieView method thunks: (*(*(void***)movie))[slot/8](movie, ...) ---
inline void* Slot(void* m, std::uintptr_t off) { return (*reinterpret_cast<void***>(m))[off / 8]; }
inline bool MIsAvail (void* m, const char* p)               { return reinterpret_cast<char(*)(void*, const char*)>(Slot(m, kMovie_IsAvailable))(m, p) != 0; }
inline int  MArrSize (void* m, const char* p)               { return reinterpret_cast<int (*)(void*, const char*)>(Slot(m, kMovie_GetVariableArraySize))(m, p); }
inline void MCreateStr(void* m, GFxValue* o, const char* s) { *o = {}; reinterpret_cast<void(*)(void*, GFxValue*, const char*)>(Slot(m, kMovie_CreateString))(m, o, s); }
inline void MCreateObj(void* m, GFxValue* o)                { *o = {}; reinterpret_cast<void(*)(void*, GFxValue*, const char*, void*, unsigned)>(Slot(m, kMovie_CreateObject))(m, o, nullptr, nullptr, 0); }
inline void MCreateFn (void* m, GFxValue* o, void* h)       { *o = {}; reinterpret_cast<void(*)(void*, GFxValue*, void*, void*)>(Slot(m, kMovie_CreateFunction))(m, o, h, nullptr); }
inline void MGetVar  (void* m, GFxValue* o, const char* p)  { *o = {}; reinterpret_cast<void(*)(void*, GFxValue*, const char*)>(Slot(m, kMovie_GetVariable))(m, o, p); }
inline char MSetVar  (void* m, const char* p, GFxValue* v)  { return reinterpret_cast<char(*)(void*, const char*, GFxValue*, int)>(Slot(m, kMovie_SetVariable))(m, p, v, 0); }
inline char MSetNum  (void* m, const char* p, double d)     { GFxValue v{}; v.type = 3; std::memcpy(&v.value, &d, sizeof d); return MSetVar(m, p, &v); }  // 3 = VT_Number
inline int  MGetNum  (void* m, const char* p)               { GFxValue v{}; MGetVar(m, &v, p); double d = 0; std::memcpy(&d, &v.value, sizeof d); return static_cast<int>(d); }
inline void MSetBool (void* m, const char* p, bool b)       { GFxValue v{}; v.type = 2; v.value = b ? 1 : 0; MSetVar(m, p, &v); }  // 2 = VT_Boolean
inline void MInvoke  (void* m, const char* p)               { reinterpret_cast<char(*)(void*, const char*, void*)>(FromRVA(kGFxMovie_Invoke))(m, p, nullptr); }  // parsed, no-arg (InvalidateData)
// Invoke AS function by path, forwarding a GFxValue ARRAY (movie vtable +0xB8): passes real objects.
inline void MInvokeArgs(void* m, const char* p, GFxValue* args, int argc) {
    reinterpret_cast<void(*)(void*, const char*, void*, GFxValue*, int)>(Slot(m, kMovie_InvokeArgs))(m, p, nullptr, args, argc);
}

// --- native GFxFunctionHandler: Call is vtable slot 1 (invoked by FUN_140fac280) ---
struct HagHandler { void** vtbl; std::int32_t refCount; std::int32_t pad; };
void* HandlerDtor(HagHandler* self, unsigned) { return self; }   // never runs: refCount pinned high
using CallFn = void (*)(HagHandler*, FnParams*);
void** MakeVtbl(CallFn call) {
    // one 8-slot vtable per distinct Call; caller passes a static storage array.
    return nullptr;  // (unused; each handler builds its own below)
}

// Trampoline an AS method: stash the original under `objPath.saveName` and install `nativeFn` as
// `objPath.methodName`. Idempotent per-movie via the AS marker (survives menu reopen: a fresh movie has
// no marker -> we re-own it). Returns true once ours is installed. Forwarding calls MInvokeArgs on the
// saved original. `handler` must be a persistent HagHandler with its vtbl set.
bool OwnMethod(void* movie, const char* objPath, const char* methodName, const char* saveName, HagHandler* handler) {
    char save[288], meth[288];
    std::snprintf(save, sizeof save, "%s.%s", objPath, saveName);
    if (MIsAvail(movie, save)) return true;                       // already owned on this movie
    std::snprintf(meth, sizeof meth, "%s.%s", objPath, methodName);
    GFxValue orig{}; MGetVar(movie, &orig, meth);
    if (orig.type == 0) { HAG_WARN("OwnMethod: {} unresolved (type 0) - cannot trampoline", meth); return false; }
    if (!MSetVar(movie, save, &orig)) { HAG_WARN("OwnMethod: failed to stash {}", save); return false; }
    GFxValue fn{}; MCreateFn(movie, &fn, handler);
    if (!MSetVar(movie, meth, &fn)) { HAG_WARN("OwnMethod: failed to install {}", meth); return false; }
    HAG_INFO("OwnMethod: trampolined {} (orig.type={:#x} stashed at {})", meth, orig.type, saveName);
    return true;
}

constexpr int kHagIndex = 90;   // marker: entry.index (main) / entry.hagIndex (system) for our row

// ================================================================================================
// MAIN MENU  --  StartMenu.onMainButtonPress dispatches by CARRIED entry.index (no positional fix-up).
// Hook FUN_140944900 (the sendMenuProperties driver): after it rebuilds MainList.entryList, (re)insert
// our row directly below Load and own onMainButtonPress. setupMainMenu ClearList()s each call, so we
// always re-insert; the method trampoline persists (it's on the StartMenu instance, not the wiped list).
// ================================================================================================
constexpr const char* kStartMenu = "_root.MenuHolder.Menu_mc";
constexpr const char* kMainList  = "_root.MenuHolder.Menu_mc.MainListHolder.List_mc";
constexpr int kLoadIndex = 2;       // StartMenu.LOAD_INDEX -- we insert directly below it

void* g_mainMovie = nullptr;
HagHandler g_mainHandler{ nullptr, 0x40000000, 0 };

void MainPress(HagHandler*, FnParams* params) {
    __try {
        void* m = g_mainMovie;
        if (!m || !params || params->argc < 1 || !params->args) return;
        char evt[288], ip[320];
        std::snprintf(evt, sizeof evt, "%s.__hagEvt", kMainList);
        MSetVar(m, evt, params->args);                       // bind the itemPress event (by reference)
        std::snprintf(ip, sizeof ip, "%s.entry.index", evt);
        if (MGetNum(m, ip) == kHagIndex) {
            HAG_INFO("HagUI main-menu row -> opening HagUI");
            OptionRender::SetContext(false);                 // Main Menu: Global pages only (no PerSave)
            HagMenu::Open();
            return;                                          // ours: do NOT forward (no vanilla cancel)
        }
        char fwd[320];                                       // vanilla row: forward to the real handler
        std::snprintf(fwd, sizeof fwd, "%s.__hagOrigMain", kStartMenu);
        MInvokeArgs(m, fwd, params->args, params->argc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void InjectMainMenu(void* movie) {
    char p[320];
    std::snprintf(p, sizeof p, "%s.entryList", kMainList);
    if (!MIsAvail(movie, p)) return;                         // list not built yet
    const int n = MArrSize(movie, p);
    if (n <= 0) return;

    // find our row (if still present after a rebuild) + Load, in one pass
    int hagPos = -1, loadPos = -1;
    for (int k = 0; k < n; ++k) {
        std::snprintf(p, sizeof p, "%s.entryList.%d.index", kMainList, k);
        const int iv = MGetNum(movie, p);
        if (iv == kHagIndex && hagPos < 0) hagPos = k;
        else if (iv == kLoadIndex && loadPos < 0) loadPos = k;
    }
    if (hagPos < 0) {                                        // (re)insert {text,index:90} directly below Load
        const int at = (loadPos >= 0) ? loadPos + 1 : n;
        for (int i = n; i > at; --i) {
            GFxValue tmp{};
            std::snprintf(p, sizeof p, "%s.entryList.%d", kMainList, i - 1); MGetVar(movie, &tmp, p);
            std::snprintf(p, sizeof p, "%s.entryList.%d", kMainList, i);     MSetVar(movie, p, &tmp);
        }
        GFxValue obj{}; MCreateObj(movie, &obj);
        std::snprintf(p, sizeof p, "%s.entryList.%d", kMainList, at);          MSetVar(movie, p, &obj);
        GFxValue str{}; MCreateStr(movie, &str, "HagUI");
        std::snprintf(p, sizeof p, "%s.entryList.%d.text", kMainList, at);     MSetVar(movie, p, &str);
        std::snprintf(p, sizeof p, "%s.entryList.%d.index", kMainList, at);    MSetNum(movie, p, kHagIndex);
        std::snprintf(p, sizeof p, "%s.entryList.%d.showIcon", kMainList, at); MSetBool(movie, p, false);
        std::snprintf(p, sizeof p, "%s.InvalidateData", kMainList);           MInvoke(movie, p);
        HAG_INFO("HagUI: inserted main-menu row below Load (pos {})", at);
    }
    if (!g_mainHandler.vtbl) {
        static void* vt[8] = {};
        for (auto& s : vt) s = reinterpret_cast<void*>(&HandlerDtor);
        vt[1] = reinterpret_cast<void*>(static_cast<CallFn>(&MainPress));
        g_mainHandler.vtbl = vt;
    }
    OwnMethod(movie, kStartMenu, "onMainButtonPress", "__hagOrigMain", &g_mainHandler);
}

using SetupFn = void (*)(void* self);
SetupFn g_origMainSetup = nullptr;
void Detour_MainSetup(void* self) {
    g_origMainSetup(self);                                   // builds MainList via setupMainMenu
    game_state::SetGameRunning(false, "MainMenu setup");
    __try {
        void* movie = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + offsets::menu_layout::kMovieView);
        if (movie) { g_mainMovie = movie; InjectMainMenu(movie); }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ================================================================================================
// IN-GAME SYSTEM MENU  --  SystemPage.onCategoryButtonPress dispatches by POSITIONAL event.index.
// We insert our row after Load and TRAMPOLINE onCategoryButtonPress: our row opens HagUI; any row after
// ours is forwarded with event.index-1 so the game's own switch maps it to the correct vanilla action.
// Trigger: JournalMenu::AdvanceMovie (one-shot per movie, gated by AS markers so it re-arms on reopen).
// ================================================================================================
constexpr const char* kSystemPage = "_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc";
constexpr const char* kSysList    = "_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc.CategoryList_mc.List_mc";
constexpr int kSysInsertAt = 3;     // directly below "Load" (onLoad pushes QuickSave,Save,Load first)

void* g_sysMovie = nullptr;
int   g_sysRow   = -1;              // cached array pos of our row (validated each dispatch)
HagHandler g_sysHandler{ nullptr, 0x40000000, 0 };

// Our row's current position (scan by the hagIndex marker; cache + revalidate). -1 if absent.
int FindSysRow(void* m) {
    char p[320];
    if (g_sysRow >= 0) {
        std::snprintf(p, sizeof p, "%s.entryList.%d.hagIndex", kSysList, g_sysRow);
        if (MGetNum(m, p) == kHagIndex) return g_sysRow;
    }
    std::snprintf(p, sizeof p, "%s.entryList", kSysList);
    const int n = MArrSize(m, p);
    for (int k = 0; k < n; ++k) {
        std::snprintf(p, sizeof p, "%s.entryList.%d.hagIndex", kSysList, k);
        if (MGetNum(m, p) == kHagIndex) { g_sysRow = k; return k; }
    }
    g_sysRow = -1;
    return -1;
}

void CatPress(HagHandler*, FnParams* params) {
    __try {
        void* m = g_sysMovie;
        if (!m || !params || params->argc < 1 || !params->args) return;
        char evt[288], q[320], fwd[320];
        std::snprintf(evt, sizeof evt, "%s.__hagEvt", kSysList);
        MSetVar(m, evt, params->args);                       // bind the itemPress event (by reference)
        std::snprintf(q, sizeof q, "%s.entry.hagIndex", evt);
        if (MGetNum(m, q) == kHagIndex) {
            HAG_INFO("HagUI System-menu row -> opening HagUI");
            game_state::SetGameRunning(false, "HagUI opened from System menu");
            OptionRender::SetContext(true);                  // in-game (save loaded): show PerSave pages
            HagMenu::Open();
            return;                                          // ours: do NOT forward
        }
        const int row = FindSysRow(m);
        std::snprintf(q, sizeof q, "%s.index", evt);
        const int P = MGetNum(m, q);
        if (row >= 0 && P > row) MSetNum(m, q, static_cast<double>(P - 1));  // undo our insert's shift
        std::snprintf(fwd, sizeof fwd, "%s.__hagOrigCat", kSystemPage);
        MInvokeArgs(m, fwd, params->args, params->argc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void SystemSetupIfNeeded(void* movie) {
    char p[320];
    std::snprintf(p, sizeof p, "%s.entryList", kSysList);
    if (!MIsAvail(movie, p)) return;                         // System page not built yet
    const int n = MArrSize(movie, p);
    if (n <= 0) return;

    // insert our row once per movie (marker-gated): after Load, shift up and set entryList[at]
    if (FindSysRow(movie) < 0) {
        const int at = (n > kSysInsertAt) ? kSysInsertAt : n;
        for (int i = n; i > at; --i) {
            GFxValue tmp{};
            std::snprintf(p, sizeof p, "%s.entryList.%d", kSysList, i - 1); MGetVar(movie, &tmp, p);
            std::snprintf(p, sizeof p, "%s.entryList.%d", kSysList, i);     MSetVar(movie, p, &tmp);
        }
        GFxValue obj{}; MCreateObj(movie, &obj);
        std::snprintf(p, sizeof p, "%s.entryList.%d", kSysList, at);          MSetVar(movie, p, &obj);
        GFxValue str{}; MCreateStr(movie, &str, "HagUI");
        std::snprintf(p, sizeof p, "%s.entryList.%d.text", kSysList, at);     MSetVar(movie, p, &str);
        std::snprintf(p, sizeof p, "%s.entryList.%d.hagIndex", kSysList, at); MSetNum(movie, p, kHagIndex);
        g_sysRow = at;
        std::snprintf(p, sizeof p, "%s.InvalidateData", kSysList);            MInvoke(movie, p);
        HAG_INFO("HagUI: inserted System-menu row after Load (pos {})", at);
    }

    if (!g_sysHandler.vtbl) {
        static void* vt[8] = {};
        for (auto& s : vt) s = reinterpret_cast<void*>(&HandlerDtor);
        vt[1] = reinterpret_cast<void*>(static_cast<CallFn>(&CatPress));
        g_sysHandler.vtbl = vt;
    }
    OwnMethod(movie, kSystemPage, "onCategoryButtonPress", "__hagOrigCat", &g_sysHandler);
}

using AdvanceFn = void (*)(void*);
AdvanceFn g_origAdvance = nullptr;
void Detour_JournalAdvance(void* self) {
    g_origAdvance(self);                                     // let the journal advance/render first
    game_state::SetGameRunning(false, "JournalMenu advance");
    __try {
        void* movie = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + offsets::menu_layout::kMovieView);
        if (movie) { g_sysMovie = movie; SystemSetupIfNeeded(movie); }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// --- grey-out fix ---------------------------------------------------------------------------------
// UpdatePermissions/RefreshSystemButtons (AS) call GameDelegate.call("SetSaveDisabled",[entryList[0..],
// BOOL]) with HARD-CODED positional indices; the native handler (0x98edb0) computes each slot's disabled
// state and writes .disabled onto the PASSED entry objects. Our insert shifts every entry after our row,
// so the AS hands the handler the wrong objects. We hook the handler and, before it runs, rewrite each
// entry arg whose live index is > our row to the entry one slot lower (the one the AS meant). The 6 entry
// args live at *(params+0x28) as consecutive 0x18 GFxValues; arg[6] is the bool. Left otherwise intact.
using SsdFn = void (*)(void* params);
SsdFn g_origSsd = nullptr;
void Detour_SetSaveDisabled(void* params) {
    __try {
        void* m = g_sysMovie;
        const int row = (m ? FindSysRow(m) : -1);
        if (m && row >= 0 && params) {
            char* args = *reinterpret_cast<char**>(reinterpret_cast<char*>(params) + 0x28);
            if (args) {
                char lp[320], p[320];
                std::snprintf(lp, sizeof lp, "%s.entryList", kSysList);
                const int n = MArrSize(m, lp);
                for (int a = 0; a < 6; ++a) {                // 6 entry objects (arg[6] = bool)
                    GFxValue* arg = reinterpret_cast<GFxValue*>(args + a * 0x18);
                    if (!arg->value) continue;
                    int idx = -1;                            // find this entry's live index
                    for (int k = row; k < n; ++k) {          // rows <= our row never shift -> skip
                        GFxValue e{}; std::snprintf(p, sizeof p, "%s.entryList.%d", kSysList, k); MGetVar(m, &e, p);
                        if (e.value == arg->value) { idx = k; break; }
                    }
                    if (idx > row && idx + 1 < n) {          // shift to the entry the AS meant
                        GFxValue corrected{}; std::snprintf(p, sizeof p, "%s.entryList.%d", kSysList, idx + 1); MGetVar(m, &corrected, p);
                        if (corrected.value) std::memcpy(arg, &corrected, sizeof(GFxValue));
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (g_origSsd) g_origSsd(params);
}

}  // namespace

// Install the entry hooks. Called from HagMenu::InstallTrigger (before Hooking::Commit).
//   * Main menu:   FUN_140944900 (list (re)build) -> insert row + own onMainButtonPress.
//   * System menu: JournalMenu::AdvanceMovie -> insert row + own onCategoryButtonPress; SetSaveDisabled
//     remap keeps the grey-out correct after our shift.
void InstallSystemInject() {
    const auto ms = offsets::FromRVA(offsets::kMainMenu_SetupList);
    if (Hooking::Create<SetupFn>(ms, &Detour_MainSetup, g_origMainSetup))
        HAG_INFO("HagUI: hooked MainMenu list-build @{:#x} (main-menu entry + AS trampoline)", ms);
    else
        HAG_ERR("HagUI: failed to hook MainMenu list-build");

    const auto adv = offsets::FromRVA(offsets::kJournalMenu_AdvanceMovie);
    if (Hooking::Create<AdvanceFn>(adv, &Detour_JournalAdvance, g_origAdvance))
        HAG_INFO("HagUI: hooked JournalMenu::AdvanceMovie @{:#x} (System-menu entry + AS trampoline)", adv);
    else
        HAG_ERR("HagUI: failed to hook JournalMenu::AdvanceMovie");

    const auto ssd = offsets::FromRVA(offsets::kSetSaveDisabled);
    if (Hooking::Create<SsdFn>(ssd, &Detour_SetSaveDisabled, g_origSsd))
        HAG_INFO("HagUI: hooked SetSaveDisabled @{:#x} (grey-out shift fix)", ssd);
    else
        HAG_ERR("HagUI: failed to hook SetSaveDisabled");
}

}  // namespace hag::ui
