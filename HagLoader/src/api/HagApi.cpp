#include "PCH.h"
#include "api/HagApi.h"
#include "Log.h"

#include <algorithm>

namespace hag::api {

// ---- Page builders -------------------------------------------------------

Page& Page::Toggle(std::string id, std::string label, bool initial, ChangeFn cb) {
    m_options.push_back({std::move(id), std::move(label), Control::Toggle, initial,
                         0, 1, 1, std::move(cb), 0});
    return *this;
}

Page& Page::Slider(std::string id, std::string label, double mn, double mx, double step,
                   double initial, ChangeFn cb, std::uint32_t debounceMs) {
    m_options.push_back({std::move(id), std::move(label), Control::Slider, initial,
                         mn, mx, step, std::move(cb), debounceMs});
    return *this;
}

Page& Page::Stepper(std::string id, std::string label, double mn, double mx, double step,
                    double initial, ChangeFn cb) {
    m_options.push_back({std::move(id), std::move(label), Control::Stepper, initial,
                         mn, mx, step, std::move(cb), 0});
    return *this;
}

Page& Page::Text(std::string id, std::string label, std::string initial, ChangeFn cb,
                 std::uint32_t debounceMs) {
    m_options.push_back({std::move(id), std::move(label), Control::Text, std::move(initial),
                         0, 0, 0, std::move(cb), debounceMs});
    return *this;
}

Page& Page::Button(std::string id, std::string label, ClickFn onClick) {
    ChangeFn wrapped = [cb = std::move(onClick)](const Value&) { if (cb) cb(); };
    m_options.push_back({std::move(id), std::move(label), Control::Button, false,
                         0, 0, 0, std::move(wrapped), 0});
    return *this;
}

Page& Page::ProgressBar(std::string id, std::string label, std::uint32_t color, SampleFn sample) {
    Option o;
    o.id = std::move(id); o.label = std::move(label); o.control = Control::ProgressBar;
    o.color = color; o.sample = std::move(sample);
    m_options.push_back(std::move(o));
    return *this;
}

Page& Page::Model3D(std::string id, std::string label, std::uint32_t formID) {
    Option o;
    o.id = std::move(id); o.label = std::move(label); o.control = Control::Model3D;
    o.formID = formID;
    m_options.push_back(std::move(o));
    return *this;
}

// ---- Host ----------------------------------------------------------------

HagUI& HagUI::Get() {
    static HagUI s;
    return s;
}

Page& HagUI::RegisterPage(std::string title, Scope scope) {
    m_pages.push_back(std::make_unique<Page>(std::move(title), scope));
    return *m_pages.back();
}

std::vector<Page*> HagUI::PagesFor(MenuContext ctx) {
    std::vector<Page*> out;
    for (auto& p : m_pages) {
        // MainMenu shows Global only; InGame shows everything.
        if (ctx == MenuContext::MainMenu && p->GetScope() != Scope::Global) continue;
        out.push_back(p.get());
    }
    // Stable tab order independent of plugin load order: Global (shared/persistent) pages first, then
    // PerSave (save-specific, e.g. Character), preserving registration order within each group.
    std::stable_sort(out.begin(), out.end(), [](const Page* a, const Page* b) {
        const int ra = (a->GetScope() == Scope::Global) ? 0 : 1;
        const int rb = (b->GetScope() == Scope::Global) ? 0 : 1;
        return ra < rb;
    });
    return out;
}

Option* HagUI::Find(const std::string& page, const std::string& id) {
    for (auto& p : m_pages) {
        if (p->m_title != page) continue;
        for (auto& o : p->m_options)
            if (o.id == id) return &o;
    }
    return nullptr;
}

void HagUI::OnControlChanged(const std::string& pageTitle, const std::string& optionId, Value v) {
    Option* opt = Find(pageTitle, optionId);
    if (!opt) return;

    if (opt->debounceMs == 0) {                 // immediate (toggles, buttons, steppers)
        opt->value = v;
        if (opt->onChange) opt->onChange(v);
        return;
    }
    // Debounced (sliders/text): keep pushing the fire time forward while the user drags;
    // it only fires after `debounceMs` of quiet, i.e. once they stop.
    for (auto& pend : m_pending) {
        if (pend.page == pageTitle && pend.id == optionId) {
            pend.value   = std::move(v);
            pend.fireAtMs = m_nowMs + opt->debounceMs;
            return;
        }
    }
    m_pending.push_back({pageTitle, optionId, std::move(v), m_nowMs + opt->debounceMs});
}

void HagUI::SetOptionState(Page* page, const std::string& id, Value value, bool enabled, std::string note) {
    if (!page) return;
    for (auto& o : page->m_options) {
        if (o.id != id) continue;
        o.value   = std::move(value);
        o.enabled = enabled;
        o.note    = std::move(note);
        return;
    }
}

void HagUI::PumpDebounce(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    for (std::size_t i = 0; i < m_pending.size();) {
        if (m_pending[i].fireAtMs <= nowMs) {
            Pending p = std::move(m_pending[i]);
            m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(i));
            if (Option* opt = Find(p.page, p.id)) {
                opt->value = p.value;
                if (opt->onChange) opt->onChange(p.value);
            }
        } else {
            ++i;
        }
    }
}

}  // namespace hag::api
