#include "PCH.h"
#include "api/HagApi.h"
#include "HagUIAPI.h"        // stable C ABI (../shared, on the include path)
#include "Log.h"

#include <variant>

// Bridges the stable cross-plugin C ABI (shared/HagUIAPI.h) onto HagUI's in-DLL
// C++ option model (hag::api::HagUI). Other SKSE plugins resolve HagUI_GetAPI via
// GetProcAddress and register pages/checkboxes that then render in the HagUI panel.
namespace {

using namespace hag::api;

inline Page* AsPage(HagUI_PageHandle* h) { return reinterpret_cast<Page*>(h); }

// Marshal the internal variant value to the flat C value handed to a callback.
HagUI_Value ToC(const Value& v) {
    HagUI_Value out{};
    if (auto* p = std::get_if<bool>(&v))              { out.type = HAGUI_VT_BOOL;   out.b = *p; }
    else if (auto* q = std::get_if<std::int64_t>(&v)) { out.type = HAGUI_VT_INT;    out.i = *q; }
    else if (auto* r = std::get_if<double>(&v))       { out.type = HAGUI_VT_DOUBLE; out.d = *r; }
    else if (auto* s = std::get_if<std::string>(&v))  { out.type = HAGUI_VT_STRING; out.s = s->c_str(); }
    return out;
}

// Wrap a C callback+user pointer into the internal std::function. In ToC the
// `const char* s` points at the live std::string, valid for the call's duration.
ChangeFn Wrap(HagUI_ChangeCb cb, void* user) {
    if (!cb) return {};
    return [cb, user](const Value& v) { cb(user, ToC(v)); };
}

HagUI_PageHandle* C_RegisterPage(const char* title, std::int32_t scope) {
    const Scope sc = (scope == HAGUI_SCOPE_PERSAVE) ? Scope::PerSave : Scope::Global;
    Page& pg = HagUI::Get().RegisterPage(title ? title : "", sc);
    HAG_INFO("HagUI API: RegisterPage('{}', {})", title ? title : "",
             sc == Scope::Global ? "Global" : "PerSave");
    return reinterpret_cast<HagUI_PageHandle*>(&pg);
}

void C_AddToggle(HagUI_PageHandle* page, const char* id, const char* label,
                 bool initial, HagUI_ChangeCb cb, void* user) {
    if (!page) return;
    AsPage(page)->Toggle(id ? id : "", label ? label : "", initial, Wrap(cb, user));
}

void C_AddSlider(HagUI_PageHandle* page, const char* id, const char* label,
                 double mn, double mx, double step, double initial,
                 HagUI_ChangeCb cb, void* user, std::uint32_t debounceMs) {
    if (!page) return;
    AsPage(page)->Slider(id ? id : "", label ? label : "", mn, mx, step, initial,
                         Wrap(cb, user), debounceMs);
}

void C_AddStepper(HagUI_PageHandle* page, const char* id, const char* label,
                  double mn, double mx, double step, double initial,
                  HagUI_ChangeCb cb, void* user) {
    if (!page) return;
    AsPage(page)->Stepper(id ? id : "", label ? label : "", mn, mx, step, initial, Wrap(cb, user));
}

void C_AddText(HagUI_PageHandle* page, const char* id, const char* label,
               const char* initial, HagUI_ChangeCb cb, void* user, std::uint32_t debounceMs) {
    if (!page) return;
    AsPage(page)->Text(id ? id : "", label ? label : "", initial ? initial : "",
                       Wrap(cb, user), debounceMs);
}

void C_AddButton(HagUI_PageHandle* page, const char* id, const char* label,
                 HagUI_ClickCb cb, void* user) {
    if (!page) return;
    ClickFn fn = cb ? ClickFn([cb, user]() { cb(user); }) : ClickFn{};
    AsPage(page)->Button(id ? id : "", label ? label : "", std::move(fn));
}

void C_SetToggleState(HagUI_PageHandle* page, const char* id, bool value, bool enabled, const char* note) {
    if (!page) return;
    HagUI::Get().SetOptionState(AsPage(page), id ? id : "", Value(value), enabled, note ? note : "");
}

void C_Refresh() { HagUI::Get().MarkDirty(); }

// v3: wrap the C bar-sample callback into the internal SampleFn (copies the transient text each poll).
SampleFn WrapBar(HagUI_BarCb cb, void* user) {
    if (!cb) return {};
    return [cb, user]() -> BarSample {
        HagUI_BarSample s = cb(user);
        return BarSample{ static_cast<double>(s.frac), s.text ? std::string(s.text) : std::string() };
    };
}

void C_AddProgressBar(HagUI_PageHandle* page, const char* id, const char* label,
                      std::uint32_t color, HagUI_BarCb sample, void* user) {
    if (!page) return;
    AsPage(page)->ProgressBar(id ? id : "", label ? label : "", color, WrapBar(sample, user));
}

void C_AddModel3D(HagUI_PageHandle* page, const char* id, const char* label, std::uint32_t formID) {
    if (!page) return;
    AsPage(page)->Model3D(id ? id : "", label ? label : "", formID);
}

LabelFn WrapLabel(HagUI_LabelCb cb, void* user, std::string fallback) {
    return [cb, user, fallback = std::move(fallback)]() -> std::string {
        if (!cb) return fallback;
        const char* label = cb(user);
        return label && *label ? std::string(label) : fallback;
    };
}

void C_AddDynamicButton(HagUI_PageHandle* page, const char* id, const char* fallbackLabel,
                        HagUI_LabelCb label, HagUI_ClickCb cb, void* user) {
    if (!page) return;
    const std::string fallback = fallbackLabel ? fallbackLabel : "";
    ClickFn fn = cb ? ClickFn([cb, user]() { cb(user); }) : ClickFn{};
    AsPage(page)->DynamicButton(id ? id : "", fallback, WrapLabel(label, user, fallback), std::move(fn));
}

// The single interface table handed to every consumer (function addresses are
// process-stable, so one shared const instance is correct).
const HagUIAPI g_api = {
    HAGUI_ABI_VERSION,
    &C_RegisterPage,
    &C_AddToggle,
    &C_AddSlider,
    &C_AddStepper,
    &C_AddText,
    &C_AddButton,
    &C_SetToggleState,
    &C_Refresh,
    &C_AddProgressBar,   // v3
    &C_AddModel3D,       // v3
    &C_AddDynamicButton,  // v4
};

}  // namespace

// Undecorated export (extern "C" + x64 => the name is exactly "HagUI_GetAPI").
// The ABI is strictly ADDITIVE (new function pointers appended), so a table built for vN is a superset
// of every older layout: a consumer that asked for v<=N reads only its prefix, which is unchanged.
// Therefore satisfy any request up to our version; only reject requests NEWER than we provide.
extern "C" __declspec(dllexport) const HagUIAPI* HagUI_GetAPI(std::uint32_t abiVersion) {
    if (abiVersion > HAGUI_ABI_VERSION) {
        HAG_WARN("HagUI_GetAPI: consumer needs ABI v{} but host only provides v{} - update HagUI",
                 abiVersion, HAGUI_ABI_VERSION);
        return nullptr;
    }
    HAG_INFO("HagUI_GetAPI: handed out option-page API v{} (consumer asked v{})", HAGUI_ABI_VERSION, abiVersion);
    return &g_api;
}
