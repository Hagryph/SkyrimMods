#pragma once
#include <cstdint>

// ============================================================================
// HagUI cross-plugin option-page API — a stable, versioned, flat C ABI.
//
// HagLoader.dll (the host) owns the option-page model (hag::api::HagUI). OTHER SKSE
// plugins register pages/checkboxes through THIS interface so their settings
// appear in the HagUI panel. Deliberately a flat C ABI — no C++ types, no
// std::function / std::variant across the DLL boundary — so it stays stable
// regardless of compiler/STL differences between plugins.
//
// Host export (compiled into HagLoader.dll, see HagLoader/src/api/HagApiExport.cpp):
//     extern "C" __declspec(dllexport) const HagUIAPI* HagUI_GetAPI(uint32_t abiVersion);
//
// Consumer (any plugin), resolved AFTER SKSE kPostPostLoad so load order can't
// race the host being present:
//     HMODULE h = GetModuleHandleW(L"HagLoader.dll");
//     auto fn = reinterpret_cast<HagUI_GetAPIFn>(h ? GetProcAddress(h, "HagUI_GetAPI") : nullptr);
//     const HagUIAPI* api = fn ? fn(HAGUI_ABI_VERSION) : nullptr;
//     if (api) {
//         HagUI_PageHandle* pg = api->RegisterPage("General", HAGUI_SCOPE_GLOBAL);
//         api->AddToggle(pg, "fullscreen", "Fullscreen", true, &OnToggle, this);
//     }
// ============================================================================

extern "C" {

// Bump ONLY for additive, backward-compatible changes. HagUI_GetAPI returns null
// for a version it cannot satisfy, so a consumer can degrade gracefully.
// v2: added SetToggleState + Refresh (per-control enabled/greyed state + note text).
// v3: added AddProgressBar (live, read-only bar) + AddModel3D (Route-A 3D character widget).
// v4: added AddDynamicButton (label callback sampled whenever HagUI rebuilds the page model).
// v5: added SetIntState + SetDoubleState for refreshing non-toggle controls after save-load.
// v6: added AddHotkey (VK-code backed keybind widget).
// v7: added AddCounter (live, read-only text counter).
// v8: added SetGridCell (two-column grid layout metadata; renderer owns pixel positions).
#define HAGUI_ABI_VERSION 8u

// Scope: Global  = shown in the Main Menu AND in-game; persists outside any save.
//        PerSave = in-game only; belongs to the loaded save.
enum {
    HAGUI_SCOPE_GLOBAL  = 0,
    HAGUI_SCOPE_PERSAVE = 1
};

// Value type tags (mirror hag::api::Value = variant<bool, int64, double, string>).
enum {
    HAGUI_VT_BOOL   = 0,
    HAGUI_VT_INT    = 1,
    HAGUI_VT_DOUBLE = 2,
    HAGUI_VT_STRING = 3
};

// A tagged value marshalled across the DLL boundary. For HAGUI_VT_STRING, `s` is
// valid ONLY for the duration of the change callback — copy it if you keep it.
typedef struct HagUI_Value {
    int32_t type;
    union {
        bool        b;
        int64_t     i;
        double      d;
        const char* s;
    };
} HagUI_Value;

// Opaque page handle (host-owned; stable for the whole session).
typedef struct HagUI_PageHandle HagUI_PageHandle;

// Control change / button-click callbacks. `user` is the pointer you passed to Add*.
typedef void (*HagUI_ChangeCb)(void* user, HagUI_Value value);
typedef void (*HagUI_ClickCb)(void* user);
typedef const char* (*HagUI_LabelCb)(void* user);
typedef const char* (*HagUI_TextCb)(void* user);

// A ProgressBar reading: fraction in [0,1] plus a short label ("80 / 100"). HagUI polls the sample
// callback every menu tick; `text` need only stay valid for that call (host copies it immediately).
typedef struct HagUI_BarSample {
    float       frac;   // 0..1 fill fraction
    const char* text;   // e.g. "80 / 100" (may be null/"" for none)
} HagUI_BarSample;
typedef HagUI_BarSample (*HagUI_BarCb)(void* user);

// The interface table. Every function pointer is non-null on a table returned by
// HagUI_GetAPI. All const char* are copied by the host; you keep ownership.
typedef struct HagUIAPI {
    uint32_t abiVersion;   // == HAGUI_ABI_VERSION for this table

    HagUI_PageHandle* (*RegisterPage)(const char* title, int32_t scope);

    void (*AddToggle)(HagUI_PageHandle* page, const char* id, const char* label,
                      bool initial, HagUI_ChangeCb onChange, void* user);
    void (*AddSlider)(HagUI_PageHandle* page, const char* id, const char* label,
                      double mn, double mx, double step, double initial,
                      HagUI_ChangeCb onChange, void* user, uint32_t debounceMs);
    void (*AddStepper)(HagUI_PageHandle* page, const char* id, const char* label,
                       double mn, double mx, double step, double initial,
                       HagUI_ChangeCb onChange, void* user);
    void (*AddText)(HagUI_PageHandle* page, const char* id, const char* label,
                    const char* initial, HagUI_ChangeCb onChange, void* user, uint32_t debounceMs);
    void (*AddButton)(HagUI_PageHandle* page, const char* id, const char* label,
                      HagUI_ClickCb onClick, void* user);

    // --- v2 ---
    // Update an already-added control's checked value, its enabled state (false => greyed out and
    // not clickable), and a small note shown under it (e.g. "applies after restart"; "" clears it).
    // Use for dependent controls (grey one toggle based on another) + restart hints.
    void (*SetToggleState)(HagUI_PageHandle* page, const char* id, bool value, bool enabled, const char* note);
    // Re-render the option panel if it's open (picks up SetToggleState changes). Cheap no-op if closed.
    void (*Refresh)();

    // --- v3 ---
    // A live, read-only progress bar (HP/Stamina/etc). `color` is 0xRRGGBB for the fill. HagUI polls
    // `sample(user)` every menu tick and redraws the bar; no user interaction / onChange.
    void (*AddProgressBar)(HagUI_PageHandle* page, const char* id, const char* label,
                           uint32_t color, HagUI_BarCb sample, void* user);
    // A 3D model widget (Route A: HagUI renders the target into an img:// virtual-image texture and
    // shows it). `formID` selects WHAT to show: 0x14 = the player (live 3D); any other TESForm id loads
    // that object's model (NPC base, static, plant, weapon, ...). 0 = player as well.
    void (*AddModel3D)(HagUI_PageHandle* page, const char* id, const char* label, uint32_t formID);

    // --- v4 ---
    // Button whose label is recomputed when the HagUI page model is built. The returned const char*
    // only needs to stay valid for the callback; the host copies it.
    void (*AddDynamicButton)(HagUI_PageHandle* page, const char* id, const char* fallbackLabel,
                             HagUI_LabelCb label, HagUI_ClickCb onClick, void* user);

    // --- v5 ---
    // Update existing numeric controls without firing their onChange callback. Use after save-load when
    // a mod reloads per-save values from HagLoader config storage.
    void (*SetIntState)(HagUI_PageHandle* page, const char* id, int64_t value, bool enabled, const char* note);
    void (*SetDoubleState)(HagUI_PageHandle* page, const char* id, double value, bool enabled, const char* note);

    // --- v6 ---
    // Rebindable keyboard shortcut widget. `initialKey` is a Win32 virtual-key code (e.g. 'V' == 0x56).
    void (*AddHotkey)(HagUI_PageHandle* page, const char* id, const char* label,
                      int64_t initialKey, HagUI_ChangeCb onChange, void* user);

    // --- v7 ---
    // A live, read-only counter/readout. HagUI polls `sample(user)` every menu tick while visible.
    void (*AddCounter)(HagUI_PageHandle* page, const char* id, const char* label,
                       HagUI_TextCb sample, void* user);

    // --- v8 ---
    // Place an existing control into HagUI's predefined two-column grid. `column` is clamped to 0..1;
    // `row` is clamped to >=0. Omit this call to keep legacy auto-flow behavior.
    void (*SetGridCell)(HagUI_PageHandle* page, const char* id, int32_t column, int32_t row);
} HagUIAPI;

// Signature of the exported resolver, for reinterpret_cast<> on the consumer side.
typedef const HagUIAPI* (*HagUI_GetAPIFn)(uint32_t abiVersion);

}  // extern "C"
