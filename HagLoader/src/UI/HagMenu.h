#pragma once

namespace hag::ui {

// HagUIMenu: our custom Scaleform menu, registered with the game's UI registry.
// Register() is safe to call live (just adds a name->creator entry). Create() is
// what the engine invokes when the menu is shown -- not exercised until we open it.
class HagMenu {
public:
    static constexpr const char* kName = "HagUIMenu";

    static void  Register();        // UI::Register(registry, "HagUIMenu", &Create)
    static void* Create();          // engine creator -> returns a new IMenu*
    static void  Open();            // UIMessageQueue::AddMessage("HagUIMenu", kShow)
    static void  Close();           // UIMessageQueue::AddMessage("HagUIMenu", kHide)
    static void  InstallTrigger();  // trampoline-hook the Main Menu to add the "HagUI" entry
};

// Installs the "HagUI" entry into the Main Menu and the in-game System menu at runtime (no SWF file):
// hooks each menu's list-build path, inserts the row, and trampolines the menu's AS click-dispatch
// method so vanilla rows keep flowing through the game's own handler. Defined in GfxInject.cpp.
void InstallSystemInject();

}  // namespace hag::ui
