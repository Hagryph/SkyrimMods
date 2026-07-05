#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <variant>
#include <functional>
#include <memory>

// HagUI host API: mods register config pages/options here; the Scaleform front-end
// (the HagUI menu, injected into the Main Menu + the in-game Escape/System menu)
// renders them and reports changes back through OnControlChanged.
namespace hag::api {

// Global  -> shown in BOTH the Main Menu and the in-game (Escape/System) menu.
//            Its values persist outside any save (a shared/global config file).
// PerSave -> shown ONLY in-game; values belong to the loaded save.
enum class Scope { Global, PerSave };

// Where a menu is being built — decides which scopes are visible.
enum class MenuContext { MainMenu, InGame };

enum class Control { Toggle, Slider, Stepper, Text, Button, ProgressBar, Model3D };

using Value    = std::variant<bool, std::int64_t, double, std::string>;
using ChangeFn = std::function<void(const Value&)>;
using ClickFn  = std::function<void()>;

// A ProgressBar reading, polled every menu tick (read-only display widget).
struct BarSample { double frac = 0.0; std::string text; };  // frac 0..1, e.g. text "80 / 100"
using SampleFn = std::function<BarSample()>;

struct Option {
    std::string   id;
    std::string   label;
    Control       control{};
    Value         value{};
    double        min = 0.0, max = 1.0, step = 1.0;   // slider / stepper
    ChangeFn      onChange;
    std::uint32_t debounceMs = 0;                      // 0 = fire immediately
    bool          enabled = true;                      // false => greyed out + not clickable
    std::string   note;                                // small hint under the control (e.g. "applies after restart")
    // --- read-only live widgets (ProgressBar / Model3D) ---
    std::uint32_t color = 0xE0B34A;                    // ProgressBar fill colour (0xRRGGBB)
    SampleFn      sample;                              // ProgressBar: polled each tick for frac + text
    std::uint32_t formID = 0x14;                       // Model3D: which TESForm to show (0x14 = player)
};

// Fluent page builder.
class Page {
public:
    Page(std::string title, Scope scope) : m_title(std::move(title)), m_scope(scope) {}

    Page& Toggle(std::string id, std::string label, bool initial, ChangeFn cb);
    Page& Slider(std::string id, std::string label, double mn, double mx, double step,
                 double initial, ChangeFn cb, std::uint32_t debounceMs = 200);
    Page& Stepper(std::string id, std::string label, double mn, double mx, double step,
                  double initial, ChangeFn cb);
    Page& Text(std::string id, std::string label, std::string initial, ChangeFn cb,
               std::uint32_t debounceMs = 300);
    Page& Button(std::string id, std::string label, ClickFn onClick);
    // Read-only live widgets (no onChange): a progress bar polled each tick, and a 3D character view.
    Page& ProgressBar(std::string id, std::string label, std::uint32_t color, SampleFn sample);
    Page& Model3D(std::string id, std::string label, std::uint32_t formID);

    const std::string&         Title()   const { return m_title; }
    Scope                      GetScope() const { return m_scope; }
    const std::vector<Option>& Options() const { return m_options; }

private:
    friend class HagUI;
    std::string         m_title;
    Scope               m_scope;
    std::vector<Option> m_options;
};

class HagUI {
public:
    static HagUI& Get();

    // Register a page. Returned reference stays valid for the session.
    Page& RegisterPage(std::string title, Scope scope);

    // Pages a given menu context should show (MainMenu => Global only; InGame => Global + PerSave).
    std::vector<Page*> PagesFor(MenuContext ctx);

    // Front-end reports a control moved. Routed through per-option debounce.
    void OnControlChanged(const std::string& pageTitle, const std::string& optionId, Value v);

    // Update a control's value / enabled (greyed) state / note without firing its onChange. A mod
    // calls this (via the C API) for dependent controls + restart hints; then Refresh() re-renders.
    void SetOptionState(Page* page, const std::string& id, Value value, bool enabled, std::string note);

    // Mark the rendered panel stale so the next menu tick re-pushes + re-draws it. TakeDirty is read
    // by the renderer each tick.
    void MarkDirty() { m_dirty = true; }
    bool TakeDirty() { const bool d = m_dirty; m_dirty = false; return d; }

    // Pump debounce timers — call every menu tick with a monotonic millisecond clock.
    void PumpDebounce(std::uint64_t nowMs);

private:
    HagUI() = default;

    struct Pending { std::string page, id; Value value; std::uint64_t fireAtMs; };

    Option* Find(const std::string& page, const std::string& id);

    std::vector<std::unique_ptr<Page>> m_pages;
    std::vector<Pending>               m_pending;
    std::uint64_t                      m_nowMs = 0;
    bool                               m_dirty = false;
};

}  // namespace hag::api
