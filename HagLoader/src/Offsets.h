#pragma once
#include "PCH.h"
#include "GameOffsets.h"  // shared single source of truth for ALL game offsets (../../shared)

// Hand-found addresses (no Address Library), recovered in Ghidra from a Steamless-decrypted
// SkyrimSE.exe 1.6.1170. RVAs are offsets from PE image base 0x140000000; convert with FromRVA().
//
// The offset VALUES now live in shared/GameOffsets.h (namespace game::ui). This header keeps the
// per-module base/FromRVA helpers and re-exports those values under the names HagUI's code uses.
namespace hag::offsets {

inline constexpr std::uintptr_t kImageBase = game::kImageBase;

inline std::uintptr_t Base() { return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr)); }
inline std::uintptr_t FromFileAddr(std::uintptr_t fileAddr) { return Base() + (fileAddr - kImageBase); }
inline std::uintptr_t FromRVA(std::uintptr_t rva) { return Base() + rva; }

// ---- re-exported from game::ui ----
inline constexpr auto kAlloc       = game::Alloc;
inline constexpr auto kHeap        = game::Heap;
inline constexpr auto kCtor1       = game::ScriptCtor1;
inline constexpr auto kCtor2       = game::ScriptCtor2;
inline constexpr auto kScriptVtbl  = game::ScriptVtbl;
inline constexpr auto kCompileRun  = game::ScriptCompileRun;
inline constexpr auto kDtor        = game::ScriptDtor;
inline constexpr auto kCompilerPtr = game::ScriptCompilerPtr;
inline constexpr auto kOffFormType = game::Script_FormType;
inline constexpr auto kOffCompiled = game::Script_Compiled;
inline constexpr auto kOffText     = game::Script_Text;
inline constexpr auto kConsolePtr  = game::ConsolePtr;
inline constexpr auto kConsData    = game::Console_Text;
inline constexpr auto kConsLen     = game::Console_Len;

// ---- re-exported from game::papyrus ----
inline constexpr auto kSkyrimVM_ptr  = game::papyrus::SkyrimVMSingletonPtr;
inline constexpr auto kSkyrimVM_Impl = game::papyrus::SkyrimVM_Impl;

// ---- re-exported from game::ui ----
inline constexpr auto kUI_ctor              = game::ui::UI_ctor;
inline constexpr auto kUI_singleton_ptr     = game::ui::UI_SingletonPtr;
inline constexpr auto kBSFixedString_ctor   = game::ui::BSFixedString_ctor;
inline constexpr auto kBSFixedString_dtor   = game::ui::BSFixedString_dtor;
inline constexpr auto kFormatString         = game::ui::FormatString;
inline constexpr auto kScaleform_LoadMovie     = game::ui::Scaleform_LoadMovie;
inline constexpr auto kScaleform_LoadMovie_alt = game::ui::Scaleform_LoadMovieAlt;
inline constexpr auto kGFxMovie_InvokePath  = game::ui::GFxMovie_InvokePath;
inline constexpr auto kGFx_LoadFile         = game::ui::GFx_LoadFile;
inline constexpr auto kScaleform_FileOpener = game::ui::Scaleform_FileOpener;
inline constexpr auto kGFxLoader_Read       = game::ui::GFxLoader_Read;
inline constexpr auto kUI_Register     = game::ui::UI_Register;
inline constexpr auto kUI_RegistryGet  = game::ui::UI_RegistryGet;
inline constexpr auto kUI_Registry_ptr = game::ui::UI_RegistryPtr;
inline constexpr auto kMenuAllocator_ptr       = game::ui::MenuAllocatorPtr;
inline constexpr auto kBSScaleformManager_ptr  = game::ui::BSScaleformManagerPtr;
inline constexpr auto kIMenu_vtable_BarterMenu = game::ui::IMenu_vtable_Barter;
inline constexpr auto kBarterMenu_Create       = game::ui::BarterMenu_Create;
inline constexpr auto kIMenu_baseCtor          = game::ui::IMenu_baseCtor;
inline constexpr auto kIMenuBase_2 = game::ui::IMenuBase_2;
inline constexpr auto kIMenuBase_3 = game::ui::IMenuBase_3;
inline constexpr auto kIMenuBase_5 = game::ui::IMenuBase_5;
inline constexpr auto kIMenuBase_6 = game::ui::IMenuBase_6;
inline constexpr auto kIMenuBase_7 = game::ui::IMenuBase_7;
inline constexpr auto kIMenuBase_8 = game::ui::IMenuBase_8;
inline constexpr auto kIMenuBase_ProcessMsg   = game::ui::IMenuBase_ProcessMsg;
inline constexpr auto kMainMenu_Create        = game::ui::MainMenu_Create;
inline constexpr auto kMainMenu_vtable        = game::ui::MainMenu_vtable;
inline constexpr auto kMainMenu_instance      = game::ui::MainMenu_instance;
inline constexpr auto kMainMenu_RegisterFuncs = game::ui::MainMenu_RegisterFuncs;
inline constexpr auto kMainMenu_SetupList     = game::ui::MainMenu_SetupList;
inline constexpr auto kJournalMenu_Create        = game::ui::JournalMenu_Create;
inline constexpr auto kJournalMenu_vtable        = game::ui::JournalMenu_vtable;
inline constexpr auto kJournalMenu_RegisterFuncs = game::ui::JournalMenu_RegisterFuncs;
inline constexpr auto kJournalMenu_AdvanceMovie  = game::ui::JournalMenu_AdvanceMovie;
inline constexpr auto kSetSaveDisabled           = game::ui::SystemPage_SetSaveDisabled;
// GFxMovieView method vtable byte-offsets (for live System-menu injection)
inline constexpr auto kMovie_IsAvailable          = game::ui::movie::IsAvailable;
inline constexpr auto kMovie_CreateString         = game::ui::movie::CreateString;
inline constexpr auto kMovie_CreateObject         = game::ui::movie::CreateObject;
inline constexpr auto kMovie_CreateFunction       = game::ui::movie::CreateFunction;
inline constexpr auto kMovie_SetVariable          = game::ui::movie::SetVariable;
inline constexpr auto kMovie_GetVariable          = game::ui::movie::GetVariable;
inline constexpr auto kMovie_GetVariableArraySize = game::ui::movie::GetVariableArraySize;
inline constexpr auto kMovie_InvokeArgs           = game::ui::movie::InvokeArgs;
inline constexpr auto kUIMessageQueue_AddMsg  = game::ui::UIMessageQueue_AddMsg;
inline constexpr auto kUIMessageQueue_ptr     = game::ui::UIMessageQueue_Ptr;
inline constexpr auto kUI_UpdateMenuState     = game::ui::UI_UpdateMenuState;
inline constexpr auto kUI_NumPausesGame       = game::ui::UI_NumPausesGame;
inline constexpr auto kGFxMovie_Invoke = game::ui::GFxMovie_InvokePath;
inline constexpr auto kGFx_InvokeInner = game::ui::GFx_InvokeInner;
inline constexpr auto kGFx_MakeString  = game::ui::GFx_MakeString;
inline constexpr auto kGFxMovie_HandleEvent_slot = game::ui::gfxview::HandleEvent;
inline constexpr int  kGFxMovie_RegisterCB       = game::ui::gfxview::RegisterCB;

inline constexpr std::uint32_t kMsg_Show = 1;  // UI_MESSAGE_TYPE::kShow
inline constexpr std::uint32_t kMsg_Hide = 3;  // kHide

namespace gfxview {
    inline constexpr auto kInvoke       = game::ui::gfxview::Invoke;
    inline constexpr auto kGetVariable  = game::ui::gfxview::GetVariable;
    inline constexpr auto kCreateView   = game::ui::gfxview::CreateView;
    inline constexpr auto kSetScaleMode = game::ui::gfxview::SetScaleMode;
    inline constexpr auto kGetVisRect   = game::ui::gfxview::GetVisRect;
    inline constexpr auto kSetViewport  = game::ui::gfxview::SetViewport;
    inline constexpr auto kDisplay      = game::ui::gfxview::Display;
}
namespace imenu_vtable {
    inline constexpr int kDtor           = game::ui::imenu::Dtor;
    inline constexpr int kRegisterFuncs  = game::ui::imenu::RegisterFuncs;
    inline constexpr int kBaseA          = game::ui::imenu::BaseA;
    inline constexpr int kBaseB          = game::ui::imenu::BaseB;
    inline constexpr int kProcessMessage = game::ui::imenu::ProcessMessage;
    inline constexpr int kBaseC          = game::ui::imenu::BaseC;
    inline constexpr int kAdvanceMovie   = game::ui::imenu::AdvanceMovie;
    inline constexpr int kBaseD          = game::ui::imenu::BaseD;
    inline constexpr int kBaseE          = game::ui::imenu::BaseE;
}
namespace menu_layout {
    inline constexpr auto kVtable    = game::ui::menu_layout::Vtable;
    inline constexpr auto kMovieView = game::ui::menu_layout::MovieView;
    inline constexpr auto kFlags     = game::ui::menu_layout::Flags;
}
// UI singleton member offsets (interned menu-name BSFixedStrings) — HagUI-local.
namespace uimenu_names {
    inline constexpr std::uintptr_t kInventoryMenu = 0x88;
    inline constexpr std::uintptr_t kJournalMenu   = 0x148;
    inline constexpr std::uintptr_t kMapMenu       = 0x110;
}

}  // namespace hag::offsets
