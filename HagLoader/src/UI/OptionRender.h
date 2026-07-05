#pragma once

// Renders the registered hag::api option pages into HagUI's live Scaleform movie
// and routes checkbox changes from AS back into hag::api::HagUI. Driven from the
// HagUI menu's per-frame tick (HagMenu.cpp :: HagTick).
namespace hag::ui {

class OptionRender {
public:
    // Push the current page/option model into the movie and call _root.HagBuildPages,
    // once per freshly-loaded movie (no-op until the SWF's frame_1 boot has run and
    // until a new movie appears). Safe to call every tick.
    static void BuildIfNeeded(void* movieView);

    // Cheap per-tick refresh of the LIVE widgets (ProgressBar fill/text) after a build. Polls each
    // bar's sample() and pushes fraction+text, then calls _root.HagUpdateBars so the AS resizes the
    // existing bar clips (no full rebuild). Call every menu tick, after BuildIfNeeded.
    static void UpdateLive(void* movieView);

    // Set the menu context from WHERE HagUI was opened: false = Main Menu (Global pages only),
    // true = in-game / a save is loaded (Global + PerSave pages). Forces a rebuild next tick.
    static void SetContext(bool inGame);
};

}  // namespace hag::ui
