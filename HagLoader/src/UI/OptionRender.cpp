#include "PCH.h"
#include "UI/OptionRender.h"
#include "UI/GfxValue.h"
#include "UI/Model3D.h"
#include "api/HagApi.h"
#include "Log.h"

#include <cstdio>
#include <variant>

// Bridges the (previously dormant) hag::api option model to HagUI's Scaleform movie:
//   C++ -> AS : PushPages() writes a flat _root.hagPage<i>_opt<j>_* model, then
//               invokes _root.HagBuildPages() to draw tabs + checkbox rows.
//   AS -> C++ : a native GFxFunctionHandler stored at _root.hagSetOption is invoked
//               as hagSetOption(pageIdx, optIdx, value) — ALL Number args, so no
//               GFxValue string-reading (only Number read-back is proven live) —
//               and routed into HagUI::OnControlChanged.
namespace hag::ui {
namespace {

using namespace hag::ui::gfx;
using namespace hag::api;

// Which pages are visible. Set at open time by OptionRender::SetContext from WHERE HagUI was opened
// (main-menu button => MainMenu = Global only; in-game System menu => InGame = Global + PerSave).
// Default MainMenu is the safe choice so PerSave pages (e.g. Character) never leak into the main menu.
MenuContext g_ctx      = MenuContext::MainMenu;
void*       g_builtView = nullptr;   // movie we last built into (rebuild when it changes)

double ValueToNum(const Value& v) {
    if (auto* b = std::get_if<bool>(&v))         return *b ? 1.0 : 0.0;
    if (auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&v))       return *d;
    return 0.0;  // Text controls aren't driven through the numeric path
}

// --- AS -> C++: _root.hagSetOption(pageIdx, optIdx, value) ---------------------
void* SetOptDtor(NativeFn* self, unsigned) { return self; }   // never runs: refCount pinned

// The body (uses C++ objects that need unwinding, so it can't live in the __try frame).
void DispatchSetOption(FnParams* params) {
    if (!params || params->argc < 3 || !params->args) return;
    const int    pageIdx = static_cast<int>(NumOf(params->args[0]));
    const int    optIdx  = static_cast<int>(NumOf(params->args[1]));
    const double raw     = NumOf(params->args[2]);

    auto pages = HagUI::Get().PagesFor(g_ctx);
    if (pageIdx < 0 || pageIdx >= static_cast<int>(pages.size())) return;
    Page* pg = pages[pageIdx];
    const auto& opts = pg->Options();
    if (optIdx < 0 || optIdx >= static_cast<int>(opts.size())) return;
    const Option& opt = opts[optIdx];

    Value v;
    switch (opt.control) {
        case Control::Slider:
        case Control::Stepper: v = raw; break;
        case Control::Text:    return;                      // not driven by this path
        default:               v = (raw != 0.0); break;     // Toggle / Button
    }
    HAG_INFO("HagUI option change: page[{}] opt[{}] id='{}' raw={}", pageIdx, optIdx, opt.id, raw);
    HagUI::Get().OnControlChanged(pg->Title(), opt.id, v);
}

// Thin SEH guard (no C++ unwinding objects here, so __try is legal).
void HagSetOptionCall(NativeFn*, FnParams* params) {
    __try { DispatchSetOption(params); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void** SetOptionVtbl() {
    static void* vt[8] = {};
    if (!vt[1]) {
        for (auto& s : vt) s = reinterpret_cast<void*>(&SetOptDtor);
        vt[1] = reinterpret_cast<void*>(&HagSetOptionCall);
    }
    return vt;
}
NativeFn g_setOption{ nullptr, 0x40000000, 0 };

void InstallSetOption(void* view) {
    if (!g_setOption.vtbl) g_setOption.vtbl = SetOptionVtbl();
    GFxValue fn{}; MCreateFn(view, &fn, &g_setOption);
    MSetVar(view, "_root.hagSetOption", &fn);
}

// --- C++ -> AS: flat page/option model ----------------------------------------
void PushPages(void* view) {
    HagUI::Get().RefreshDynamicLabels();
    auto pages = HagUI::Get().PagesFor(g_ctx);
    char p[128];
    MSetNum(view, "_root.hagPageCount", static_cast<double>(pages.size()));
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        Page* pg = pages[i];
        std::snprintf(p, sizeof p, "_root.hagPage%d_title", i);    MSetStr(view, p, pg->Title().c_str());
        std::snprintf(p, sizeof p, "_root.hagPage%d_scope", i);    MSetNum(view, p, pg->GetScope() == Scope::Global ? 0.0 : 1.0);
        const auto& opts = pg->Options();
        std::snprintf(p, sizeof p, "_root.hagPage%d_optCount", i); MSetNum(view, p, static_cast<double>(opts.size()));
        for (int j = 0; j < static_cast<int>(opts.size()); ++j) {
            const Option& o = opts[j];
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_label",   i, j); MSetStr(view, p, o.label.c_str());
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_type",    i, j); MSetNum(view, p, static_cast<double>(static_cast<int>(o.control)));
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_value",   i, j); MSetNum(view, p, ValueToNum(o.value));
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_enabled", i, j); MSetNum(view, p, o.enabled ? 1.0 : 0.0);
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_note",    i, j); MSetStr(view, p, o.note.c_str());
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_color",   i, j); MSetNum(view, p, static_cast<double>(o.color));
            // seed the live fields so the bar draws immediately (before the first UpdateLive tick)
            if (o.control == Control::ProgressBar && o.sample) {
                BarSample s = o.sample();
                std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_fill",    i, j); MSetNum(view, p, s.frac);
                std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_bartext", i, j); MSetStr(view, p, s.text.c_str());
            }
        }
    }
}

// Per-tick live refresh: poll each ProgressBar's sample() and push fraction+text, then let the AS
// resize the existing bar clips. Separate from PushPages (which is a full, rare rebuild). C++ objects
// live here (not in the __try guard below) to satisfy MSVC's no-unwind-in-__try rule.
void DoUpdateLive(void* view) {
    auto pages = HagUI::Get().PagesFor(g_ctx);
    char p[128];
    bool any = false;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        const auto& opts = pages[i]->Options();
        for (int j = 0; j < static_cast<int>(opts.size()); ++j) {
            const Option& o = opts[j];
            if (o.control != Control::ProgressBar || !o.sample) continue;
            BarSample s = o.sample();
            double f = s.frac < 0.0 ? 0.0 : (s.frac > 1.0 ? 1.0 : s.frac);
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_fill",    i, j); MSetNum(view, p, f);
            std::snprintf(p, sizeof p, "_root.hagPage%d_opt%d_bartext", i, j); MSetStr(view, p, s.text.c_str());
            any = true;
        }
    }
    if (any) MInvoke(view, "_root.HagUpdateBars");
}

}  // namespace

void OptionRender::BuildIfNeeded(void* view) {
    __try {
        if (!view) return;
        if (!MIsAvail(view, "_root.hagReady")) return;   // SWF frame_1 boot not finished yet
        // Rebuild on a fresh movie OR when a mod marked the model dirty (SetToggleState + Refresh).
        const bool dirty = HagUI::Get().TakeDirty();
        if (view == g_builtView && !dirty) return;
        InstallSetOption(view);
        // Ensure the Model3D img:// texture exists + tell the SWF it can loadMovie("img://hagCharModel").
        MSetNum(view, "_root.hagModelReady", Model3D::EnsureTexture() ? 1.0 : 0.0);
        PushPages(view);
        MInvoke(view, "_root.HagBuildPages");
        g_builtView = view;
        HAG_INFO("HagUI: (re)built option pages into movie {} (dirty={})", view, dirty);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void OptionRender::UpdateLive(void* view) {
    __try {
        if (!view || view != g_builtView) return;         // only after a successful build
        if (!MIsAvail(view, "_root.HagUpdateBars")) return;
        DoUpdateLive(view);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void OptionRender::SetContext(bool inGame) {
    g_ctx = inGame ? MenuContext::InGame : MenuContext::MainMenu;
    g_builtView = nullptr;   // force a rebuild so the visible tab set matches the new context
}

}  // namespace hag::ui
