#include "PCH.h"
#include "HagUIBridge.h"
#include "Config.h"
#include "Display.h"
#include "SettingsHook.h"
#include "Log.h"

namespace hag {
namespace {

const HagUIAPI*   g_api  = nullptr;   // resolved HagUI option-page API
HagUI_PageHandle* g_page = nullptr;   // our "General" page

bool AsBool(const HagUI_Value& v) {
    return (v.type == HAGUI_VT_BOOL) ? v.b : (v.i != 0);
}

// Recompute the greyed state / value / restart-note of the three checkboxes from config + the mode
// this session actually booted in, push them to HagUI, and refresh the panel. Rules (per design):
//   Fullscreen  -> always "applies after restart" (it can only change at next launch).
//   Borderless  -> greyed + forced ON while Fullscreen is checked (irrelevant under fullscreen);
//                  "applies after restart" only when THIS session booted fullscreen (can't switch live).
//   AlwaysActive-> live, no note.
void PushGeneralStates() {
    if (!g_api || !g_api->SetToggleState || !g_api->Refresh || !g_page) return;   // HagUI too old for v2
    const Config& c = Config::Get();
    const bool sessionFs = Display::SessionFullscreen();

    g_api->SetToggleState(g_page, "fullscreen", c.fullscreen, true, "applies after restart");

    const bool  bdEnabled = !c.fullscreen;                         // greyed while Fullscreen is on
    const bool  bdValue   = c.fullscreen ? true : c.borderless;    // auto-set to 1 under fullscreen
    const char* bdNote    = (!c.fullscreen && sessionFs) ? "applies after restart" : "";
    g_api->SetToggleState(g_page, "borderless", bdValue, bdEnabled, bdNote);

    g_api->SetToggleState(g_page, "alwaysActive", c.alwaysActive, true, "");
    g_api->Refresh();
}

// --- checkbox change callbacks (invoked by HagUI when the user toggles a box) ---
void OnFullscreen(void*, HagUI_Value v) {
    Config& c = Config::Get();
    c.fullscreen = AsBool(v);
    if (c.fullscreen) c.borderless = true;   // borderless is implied/irrelevant under exclusive fullscreen
    c.Save();
    SettingsHook::Apply();   // updates the startup-read flags -> effect on NEXT LAUNCH (no live blackscreen)
    PushGeneralStates();     // grey/ungrey Borderless + refresh notes
    HAG_INFO("General: Fullscreen = {} (applies on next launch)", c.fullscreen);
}
void OnBorderless(void*, HagUI_Value v) {
    Config& c = Config::Get();
    c.borderless = AsBool(v);
    c.Save();
    SettingsHook::Apply();
    Display::ApplyBorderlessLive();   // live Win32 window-style switch — only if THIS session booted windowed
    PushGeneralStates();
    HAG_INFO("General: Borderless = {}{}", c.borderless,
             Display::SessionFullscreen() ? " (applies on next launch)" : "");
}
void OnAlwaysActive(void*, HagUI_Value v) {
    Config& c = Config::Get();
    c.alwaysActive = AsBool(v);
    c.Save();
    SettingsHook::Apply();   // the game's pause check reads this flag every frame
    HAG_INFO("General: AlwaysActive = {}", c.alwaysActive);
}

}  // namespace

void HagUIBridge::Register(HagUI_PageHandle* page) {
    HMODULE h = ::GetModuleHandleW(L"HagUI.dll");
    if (!h) { HAG_WARN("HagUI.dll not loaded - General settings still apply, but no UI page is shown"); return; }

    auto getApi = reinterpret_cast<HagUI_GetAPIFn>(::GetProcAddress(h, "HagUI_GetAPI"));
    if (!getApi) { HAG_WARN("HagUI_GetAPI export not found - update HagUI to a build with the option-page API"); return; }

    const HagUIAPI* api = getApi(HAGUI_ABI_VERSION);
    if (!api) { HAG_WARN("HagUI_GetAPI returned null (ABI version mismatch - rebuild HagUI for v{})", HAGUI_ABI_VERSION); return; }

    const Config& c = Config::Get();
    g_api  = api;
    g_page = page;
    if (!g_page) {
        HAG_WARN("HagGeneral received a null HagUI page");
        return;
    }
    api->AddToggle(g_page, "fullscreen",   "Fullscreen",    c.fullscreen,   &OnFullscreen,   nullptr);
    api->AddToggle(g_page, "borderless",   "Borderless",    c.borderless,   &OnBorderless,   nullptr);
    api->AddToggle(g_page, "alwaysActive", "Always Active", c.alwaysActive, &OnAlwaysActive, nullptr);
    PushGeneralStates();   // initial greyed state + restart notes

    HAG_INFO("registered 'General' page with HagUI (fullscreen={} borderless={} alwaysActive={})",
             c.fullscreen, c.borderless, c.alwaysActive);
}

}  // namespace hag
