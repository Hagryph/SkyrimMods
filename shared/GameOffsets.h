#pragma once
#include <cstdint>
#include <cstddef>

// ============================================================================
// Shared Skyrim SE 1.6.1170 offsets, hand-found in Ghidra (NO Address Library).
// All values are RVAs off the image base 0x140000000; add the live module base
// (offsets::FromRVA / GetModuleHandle(NULL) + rva) before use.
//
// Single source of truth for EVERY project in SkyrimMods (HagIPC, HagLoader, external mods, ...).
// If an offset is wrong, fix it here once. Grouped by subsystem; each entry has
// the Ghidra symbol it came from so it can be re-verified.
// ============================================================================
namespace game {

// ---- image ----
inline constexpr std::uintptr_t kImageBase = 0x140000000;

// ---- game allocator (MemoryManager) ----
inline constexpr std::uintptr_t Alloc      = 0xcc40c0;    // FUN_140cc40c0(&heap,size,align,flag)
inline constexpr std::uintptr_t Free       = 0xcc4510;    // FUN_140cc4510(&heap,ptr,flag)
inline constexpr std::uintptr_t Heap       = 0x35f11e0;   // &DAT_1435f11e0  heap handle
inline constexpr std::uintptr_t HeapInit   = 0x35f11d0;   // DAT_1435f11d0   heap-init flag (==2)

// ---- console command execution (build a transient Script + CompileAndRun) ----
inline constexpr std::uintptr_t ScriptCtor1       = 0x1dd960;   // FUN_1401dd960
inline constexpr std::uintptr_t ScriptCtor2       = 0x1e0ba0;   // FUN_1401e0ba0 (ORs 0x4000 into +0x10)
inline constexpr std::uintptr_t ScriptVtbl        = 0x17c1618;  // &PTR_FUN_1417c1618
inline constexpr std::uintptr_t ScriptCompileRun  = 0x33d6a0;   // FUN_14033d6a0(script,compiler,type,refr)
inline constexpr std::uintptr_t ScriptDtor        = 0x1ddc90;   // FUN_1401ddc90 (frees text/data, unregisters)
inline constexpr std::uintptr_t ScriptCompilerPtr = 0x31acb58;  // *(ScriptCompiler**) global console compiler
inline constexpr std::uintptr_t SelectedRefHandle = 0x31acd74;  // console selected-ref handle (uint32)
inline constexpr std::uintptr_t ResolveRefHandle  = 0x1795f0;   // FUN_1401795f0(handle*, TESObjectREFR**)
// Script field offsets
inline constexpr std::size_t Script_FormType = 0x1a;    // = 0x13 (FormType::Script)
inline constexpr std::size_t Script_Compiled = 0x28;    // compiled bytecode size (!=0 == ok)
inline constexpr std::size_t Script_Text     = 0x38;    // char* command text (game heap)
inline constexpr std::size_t Script_Data     = 0x40;    // char* compiled data

// ---- console output buffer (FUN_1408f9220 appends every printed line here) ----
inline constexpr std::uintptr_t ConsolePtr  = 0x3137ee0;  // *(Console**) global console object
inline constexpr std::size_t Console_Text = 0x408;   // char*  accumulated output
inline constexpr std::size_t Console_Len  = 0x410;   // uint16 length
inline constexpr std::size_t Console_Cap  = 0x412;   // uint16 capacity

// ---- save / load (console `load` path FUN_14036ebb0 -> FUN_140610480) ----
inline constexpr std::uintptr_t SaveLoadManagerPtr = 0x317abf0;  // *(BGSSaveLoadManager**)
inline constexpr std::uintptr_t SaveLoad_BeginLoad = 0x649610;   // FUN_140649610()  begin-load teardown
inline constexpr std::uintptr_t SaveLoad_Load      = 0x610480;   // FUN_140610480(mgr,name,0xffffffff,flags,1)
inline constexpr std::uintptr_t SaveLoad_LoadFlag  = 0x31872b1;  // DAT_1431872b1 (byte)=1 before load
inline constexpr std::uintptr_t SaveLoad_Populate  = 0x611f50;   // FUN_140611f50(mgr) enumerate+populate list
inline constexpr std::size_t Mgr_ListCount = 0x7c;   // uint32 character/entry count
inline constexpr std::size_t Mgr_CharList  = 0x98;   // entry[] base (stride 0x18: id, name*, valid)
// The game's own save-directory resolver (VFS-correct: MO2 redirects to ...\__MO_Saves\).
inline constexpr std::uintptr_t SaveDirSingleton = 0x152e130;  // FUN_14152e130() -> save-task singleton
inline constexpr std::size_t    SaveDir_VtblSlot = 8;          // vtable[8]: void(this, char* out, 0)

// ============================================================================
// Settings (used by HagGeneral). These are STATIC Setting objects (0x18 each:
// vtable@0x00, value@0x08, name*@0x10) found in Ghidra via their name strings and
// VERIFIED LIVE via HagIPC. HagGeneral overwrites the *value* fields from its own
// config every frame, so the game (and any console command / other mod) reads our
// values. Display mode is additionally imposed live via a D3D swapchain hook.
// ============================================================================
namespace settings {
// bAlwaysActive is read EVERY FRAME by the main loop's focus check (FUN_14063e970 /
// FUN_140641ad0): if the window is inactive AND this is 0, the loop Sleeps instead of
// stepping the sim (the pause). Non-zero => always simulate. Verified: writing 1 keeps
// the game running on alt-tab.
inline constexpr std::uintptr_t BAlwaysActive = 0x20124d8;  // value of Setting "bAlwaysActive:General" (@0x20124d0)
inline constexpr std::uintptr_t BFullScreen   = 0x202b270;  // value of Setting "bFull Screen:Display" (@0x202b268)
inline constexpr std::uintptr_t BBorderless   = 0x202b288;  // value of Setting "bBorderless:Display"  (@0x202b280)
}  // namespace settings

namespace alwaysactive {
// The THREE RIP-relative CMP reads of bAlwaysActive (settings::BAlwaysActive) that gate the pause:
// two in the main loop's focus check (FUN_14063e970 / FUN_140641ad0) and one in SetActive
// (FUN_140649920). Rather than fight other writers of the setting, HagGeneral repoints each
// instruction's disp32 to its OWN flag byte, so ONLY our mod drives the pause and a console
// SetINISetting / another mod writing bAlwaysActive is simply not read here. Verified disassembly:
//   Read1 @0x63ea73  CMP byte[bAlwaysActive],SIL   40 38 35 <disp32>      disp@+3, len 7
//   Read2 @0x641b92  CMP byte[bAlwaysActive],SIL   40 38 35 <disp32>      disp@+3, len 7
//   Read3 @0x64992a  CMP byte[bAlwaysActive],0x0   80 3d <disp32> 00      disp@+2, len 7
inline constexpr std::uintptr_t Read1 = 0x63ea73;
inline constexpr std::uintptr_t Read2 = 0x641b92;
inline constexpr std::uintptr_t Read3 = 0x64992a;
inline constexpr int Read1Disp = 3, Read2Disp = 3, Read3Disp = 2;   // disp32 byte offset within each
inline constexpr int ReadLen = 7;                                    // all three are 7-byte instructions
}  // namespace alwaysactive

namespace display {
// The TWO startup reads of the display settings, both MOVZX EAX,byte[rip+disp32] (7 bytes, disp@+3),
// inside FUN_140640120; the values flow into FUN_140e423e0's window/swapchain creation. Each value is
// read by ONLY this function (DataRefs), i.e. read nowhere else — so repointing these two reads at
// our own flag bytes makes the game natively build the window + swapchain from OUR config at startup.
//   FullScreenRead @0x640cff  MOVZX EAX,byte[bFull Screen]  0f b6 05 <disp32>
//   BorderlessRead @0x640d0a  MOVZX EAX,byte[bBorderless]   0f b6 05 <disp32>
inline constexpr std::uintptr_t FullScreenRead = 0x640cff;
inline constexpr std::uintptr_t BorderlessRead = 0x640d0a;
inline constexpr int FullScreenDisp = 3, BorderlessDisp = 3;   // disp32 byte offset (len == alwaysactive::ReadLen == 7)
}  // namespace display

// ============================================================================
// UI / Scaleform (used by HagUI): menu registration, GFx movie, IMenu vtable.
// ============================================================================
namespace ui {

inline constexpr std::uintptr_t UI_ctor              = 0xFB7E10;   // interns menu names; sets UI singleton
inline constexpr std::uintptr_t UI_SingletonPtr      = 0x20F8958;  // *(UI**)
inline constexpr std::uintptr_t BSFixedString_ctor   = 0xCEC5D0;   // (BSFixedString*, const char*)
inline constexpr std::uintptr_t BSFixedString_dtor   = 0xCEC740;   // (BSFixedString*)
inline constexpr std::uintptr_t FormatString         = 0x1C3CD0;   // sprintf-into-BSString

// Scaleform
inline constexpr std::uintptr_t Scaleform_LoadMovie    = 0xFB0110; // BSScaleformManager::LoadMovie
inline constexpr std::uintptr_t Scaleform_LoadMovieAlt = 0xFB0980;
inline constexpr std::uintptr_t Scaleform_FileOpener   = 0xFB4490; // BSScaleformFileOpener::Open
inline constexpr std::uintptr_t GFx_LoadFile           = 0x1034E30;
inline constexpr std::uintptr_t GFxLoader_Read         = 0x10323F0;
inline constexpr std::uintptr_t GFxMovie_InvokePath    = 0xFBFB10; // Invoke(view,"_root.path",args)
inline constexpr std::uintptr_t GFx_InvokeInner        = 0x1021AA0;
inline constexpr std::uintptr_t GFx_MakeString         = 0xFBB150;
inline constexpr std::uintptr_t BSScaleformManagerPtr  = 0x35F11C8; // *(BSScaleformManager**)

// Menu registration / creation ABI
inline constexpr std::uintptr_t UI_Register           = 0xFA5480;  // (registry, name, IMenu*(*creator)())
inline constexpr std::uintptr_t UI_RegistryGet        = 0xFA32F0;  // menu-registry singleton getter
inline constexpr std::uintptr_t UI_RegistryPtr        = 0x20F6A00; // cached registry global
inline constexpr std::uintptr_t MenuAllocatorPtr      = 0x3292490; // memory mgr (Allocate @ vtable+0x50)
inline constexpr std::uintptr_t IMenu_vtable_Barter   = 0x18F0CE0; // 9-slot IMenu vtable template
inline constexpr std::uintptr_t BarterMenu_Create     = 0x8EF2B0;  // example creator
inline constexpr std::uintptr_t IMenu_baseCtor        = 0xFAECC0;  // IMenu/BSInputEventUser base ctor
// Reusable generic IMenu vtable-slot impls
inline constexpr std::uintptr_t IMenuBase_2           = 0x573990;
inline constexpr std::uintptr_t IMenuBase_3           = 0x5739A0;
inline constexpr std::uintptr_t IMenuBase_5           = 0xFAEDB0;
inline constexpr std::uintptr_t IMenuBase_6           = 0x5739C0;  // generic tick
inline constexpr std::uintptr_t IMenuBase_7           = 0xFAEE60;
inline constexpr std::uintptr_t IMenuBase_8           = 0xFAEE70;
inline constexpr std::uintptr_t IMenuBase_ProcessMsg  = 0xFAED60;  // base ProcessMessage (forwards input)

// Injection targets
inline constexpr std::uintptr_t MainMenu_Create        = 0x947410;
inline constexpr std::uintptr_t MainMenu_vtable        = 0x18FC980;
inline constexpr std::uintptr_t MainMenu_instance      = 0x31AEEE8; // *(MainMenu**)
inline constexpr std::uintptr_t MainMenu_RegisterFuncs = 0x941CC0;
// MainMenu function that (re)builds the main-menu list: assembles 15 GFxValues and Invokes the AS
// GameDelegate callback "sendMenuProperties" -> StartMenu.setupMainMenu, which pushes the entries onto
// MainList.entryList. Hooking this + injecting AFTER the original = insert our row right after the game
// finishes registering its own entries (called on every list (re)build from MainMenu::ProcessMessage).
inline constexpr std::uintptr_t MainMenu_SetupList     = 0x944900;  // FUN_140944900
inline constexpr std::uintptr_t JournalMenu_Create        = 0x995CD0;
inline constexpr std::uintptr_t JournalMenu_vtable        = 0x190AF38;
inline constexpr std::uintptr_t JournalMenu_RegisterFuncs = 0x994C10;  // vtable[1]; registers GameDelegate cbs
inline constexpr std::uintptr_t JournalMenu_AdvanceMovie  = 0x994BC0;  // vtable[6]; per-frame while the journal is active
// System-page "SetSaveDisabled" GameDelegate handler: receives 6 entry OBJECTS + a bool, computes each
// entry's disabled state from game save-state, sets .disabled on the passed objects (FUN_140fae210).
inline constexpr std::uintptr_t SystemPage_SetSaveDisabled = 0x98EDB0;

// GFxMovieView method vtable BYTE-offsets (validated live 2026-07-01 + decompiled; docs/UI-RE.md §10a).
// Call as (*(*(void***)movie))[off/8](movie, ...). GFxValue is 0x18 bytes; zero the out-buffer first.
namespace movie {
    inline constexpr std::uintptr_t IsAvailable          = 0x50;   // (movie, const char* path) -> bool
    inline constexpr std::uintptr_t CreateString         = 0x58;   // (movie, GFxValue* out, const char*)
    inline constexpr std::uintptr_t CreateObject         = 0x68;   // (movie, out, className=0, args=0, nargs=0)
    inline constexpr std::uintptr_t CreateFunction       = 0x78;   // (movie, out, GFxFunctionHandler*, userData)
    inline constexpr std::uintptr_t SetVariable          = 0x80;   // (movie, path, GFxValue*, setType); 0 = create leaf on existing parent
    inline constexpr std::uintptr_t GetVariable          = 0x88;   // (movie, GFxValue* out, path)
    inline constexpr std::uintptr_t GetVariableArraySize = 0xA0;   // (movie, path) -> int
    // Invoke an AS function by path, passing a GFxValue ARRAY (unlike +0xB0 which is printf-style and
    // cannot pass an existing object). This is GFxMovieView::Invoke(args) = FUN_141009c80, confirmed live
    // at vtable slot +0xB8. Signature: (movie, const char* path, GFxValue* result_or_null, GFxValue* args,
    // int numArgs). Used to forward a trampolined AS handler to the saved original with the same event arg.
    inline constexpr std::uintptr_t InvokeArgs           = 0xB8;   // (movie, path, result, GFxValue* args, int numArgs)
}
// GFxFunctionHandler (from CreateFunction): { void** vtable; int32 refCount; }; Call is vtable[1]
// (movie invokes it as handler->vtable[1](handler, Params*) in FUN_140fac280).

// Open/close a menu
inline constexpr std::uintptr_t UIMessageQueue_AddMsg = 0x1AF260;
inline constexpr std::uintptr_t UIMessageQueue_Ptr    = 0x20F8950; // *(UIMessageQueue**)
// Called by UI::ProcessMessages after menu open/close updates the counters at UI+0x160..0x178.
// This is the engine-level point where pause/menu-mode state has just changed.
inline constexpr std::uintptr_t UI_UpdateMenuState = 0xFA5E70; // FUN_140fa5e70(UI*, topMenu, ...)
inline constexpr std::size_t    UI_NumPausesGame   = 0x160;    // uint32 UI::numPausesGame

// GFxMovieView vtable SLOT byte-offsets (call (*(*view+slot))(view,...))
namespace gfxview {
    inline constexpr std::uintptr_t Invoke       = 0x50;
    inline constexpr std::uintptr_t GetVariable  = 0x88;
    inline constexpr std::uintptr_t CreateView   = 0xC0;
    inline constexpr std::uintptr_t SetScaleMode = 0xD8;
    inline constexpr std::uintptr_t GetVisRect   = 0xF8;
    inline constexpr std::uintptr_t SetViewport  = 0x118;
    inline constexpr std::uintptr_t Display      = 0x128;
    inline constexpr std::uintptr_t HandleEvent  = 0x168;  // input-event slot
    inline constexpr int            RegisterCB   = 1;      // slot +0x08: register native fn
}
// IMenu vtable slot indices (9 slots)
namespace imenu {
    inline constexpr int Dtor = 0, RegisterFuncs = 1, BaseA = 2, BaseB = 3,
                         ProcessMessage = 4, BaseC = 5, AdvanceMovie = 6, BaseD = 7, BaseE = 8;
}
// Menu object layout
namespace menu_layout { inline constexpr std::uintptr_t Vtable = 0x00, MovieView = 0x10, Flags = 0x20; }

}  // namespace ui

// ============================================================================
// Actor / player: player singleton + actor-value reads.
// Hand-found in Ghidra (console GetActorValue/percent handlers FUN_14032a260/
// FUN_140666f60; HUD updater FUN_140922850) and VERIFIED LIVE via HagIPC:
// *(0x31874F8) has formID 0x14 (the player) + formType 0x3E (Character).
// ============================================================================
namespace actor {

inline constexpr std::uintptr_t PlayerSingletonPtr    = 0x31874F8;  // *(Actor**) g_thePlayer (formID 0x14)
inline constexpr std::size_t    ActorValueOwnerOffset = 0xB8;       // Actor -> ActorValueOwner subobject (its vtable ptr)

// ActorValueOwner vtable slot indices. Call as: avo = actor+0xB8; vt = *(void***)avo;
//   float v = ((float(*)(void* avo, uint32_t av))vt[slot])(avo, av);
inline constexpr int AVOwner_GetActorValue          = 1;  // +0x08 current value (base+permanent+temporary+damage)
inline constexpr int AVOwner_GetPermanentActorValue = 2;  // +0x10 max (base+permanent modifiers, excludes damage)
inline constexpr int AVOwner_GetBaseActorValue      = 3;  // +0x18 base
inline constexpr int AVOwner_SetBaseActorValue      = 4;  // +0x20 base value; CommonLib ActorValueOwner::SetBaseActorValue
inline constexpr int AVOwner_SetActorValue          = 7;  // +0x38

// ActorValue indices
inline constexpr std::uint32_t AV_Health  = 0x18;
inline constexpr std::uint32_t AV_Magicka = 0x19;
inline constexpr std::uint32_t AV_Stamina = 0x1A;
inline constexpr std::uint32_t AV_MagickaRate = 0x1C;
inline constexpr std::uint32_t AV_StaminaRate = 0x1D;
inline constexpr std::uint32_t AV_SpeedMult = 0x1E;
inline constexpr std::uint32_t AV_Variable08 = 75;

// Actor value mutation path. Reverse engineered in Ghidra from the direct Health
// consumers: this clamps/writes modifier values and calls Actor::HandleHealthDamage
// for modifier 2 + AV_Health + negative delta.
inline constexpr std::uintptr_t Actor_ModActorValueInternal = 0x6B27A0;
inline constexpr std::uintptr_t Actor_SetDeadState = 0x665230;  // Actor* RCX, bool DL; tail-calls process dead-state setter
inline constexpr std::int32_t   AVModifier_Damage = 2;

// Actor layout / process helpers.
inline constexpr std::size_t ActorProcessOffset = 0xF8;          // Actor -> AIProcess*
inline constexpr std::uintptr_t AIProcess_SetupSpecialIdle = 0x6DDE70;  // Address Library 1.6.1170 id 39256
// Papyrus Actor.StartVampireFeed wrapper FUN_1409eb0f0 calls this on the
// target actor's AIProcess, then resolves the returned handle as the third
// InitiateVampireFeedPackage argument. For sleeping victims this is the active
// bed/bedroll/furniture ref that drives the separate bed-feed animation route.
inline constexpr std::uintptr_t AIProcess_GetCurrentFurnitureHandle = 0x7127C0;  // FUN_1407127c0(AIProcess*, ObjectRefHandle*)
inline constexpr std::uint32_t DefaultObject_ActionIdle = 64;    // DEFAULT_OBJECT::kActionIdle
inline constexpr std::size_t ActorStateOffset = 0xC0;            // CommonLib Actor::AsActorState() for 1.6.1170
inline constexpr std::size_t ActorState_State1 = ActorStateOffset + 0x08;  // ActorState::actorState1
inline constexpr std::size_t ActorState_State2 = ActorStateOffset + 0x0C;  // ActorState::actorState2
inline constexpr std::uint32_t ActorState1_SneakingMask = 1u << 9;
inline constexpr int ActorState1_SitSleepShift = 14;
inline constexpr std::uint32_t ActorState1_SitSleepMask = 0xFu << ActorState1_SitSleepShift;
inline constexpr int ActorState1_LifeStateShift = 21;
inline constexpr std::uint32_t ActorState1_LifeStateMask = 0xFu << ActorState1_LifeStateShift;
inline constexpr int ActorState2_WeaponStateShift = 5;
inline constexpr std::uint32_t ActorState2_WeaponStateMask = 0x7u << ActorState2_WeaponStateShift;

// Papyrus Actor.HasLOS wrapper FUN_1409e8af0 calls this exact native helper:
//   FUN_1409ba120(requester, target, bool* targetIsActorOut)
// For sneak feed parity with Better Vampires we call it as target.HasLOS(player).
inline constexpr std::uintptr_t Actor_HasLineOfSight = 0x9BA120;

// Actor virtual slots from CommonLibSSE-NG headers, verified against Skyrim SE 1.6.1170 layout.
inline constexpr int VSlot_IsChild = 0x05E;                      // bool TESObjectREFR/Actor::IsChild()
inline constexpr int VSlot_IsDead = 0x099;                       // bool Actor::IsDead(bool notEssential)
inline constexpr int VSlot_GetCannibal = 0x0BC;                  // bool Actor::GetCannibal()
inline constexpr int VSlot_SetCannibal = 0x0BD;                  // void Actor::SetCannibal(bool)
inline constexpr int VSlot_GetVampireFeed = 0x0BE;               // bool Actor::GetVampireFeed(); byte slot +0x5F0
inline constexpr int VSlot_SetVampireFeed = 0x0BF;               // void Actor::SetVampireFeed(bool)
inline constexpr int VSlot_InitiateVampireFeedPackage = 0x0C0;   // live-victim package; Papyrus StartVampireFeed rejects dead targets before this
inline constexpr int VSlot_InitiateCannibalPackage = 0x0C1;      // corpse-feed package; Papyrus Actor.StartCannibal routes here
inline constexpr int VSlot_IsInCombat = 0x0E3;                   // bool Actor::IsInCombat()
inline constexpr int VSlot_CalculateCachedOwnerIsUndead = 0x115; // bool Actor::CalculateCachedOwnerIsUndead()
inline constexpr int VSlot_CalculateCachedOwnerIsNPC = 0x116;    // bool Actor::CalculateCachedOwnerIsNPC()

}  // namespace actor

namespace vtable {

// CommonLibSSE-NG VTABLE_Character AE Address Library ID 207886, resolved through
// versionlib-1-6-1170-0.bin. Character is the concrete non-player actor class used
// by child NPCs; slot 0x05E is Character/Actor::IsChild().
inline constexpr std::uintptr_t Character = 0x18A5558;

}  // namespace vtable

namespace refr {

inline constexpr int VSlot_GetCurrent3D = 0x08D;  // TESObjectREFR::GetCurrent3D()
inline constexpr std::uintptr_t GetCalcLevel = 0x2FA590;  // Address Library 1.6.1170 id 20205: TESObjectREFR::GetCalcLevel(bool)
inline constexpr std::uintptr_t ApplyEffectShader = 0x2F0E20;  // Address Library 1.6.1170 id 19872: TESObjectREFR::ApplyEffectShader
inline constexpr std::uintptr_t SetLocation = 0x2EA960;  // TESObjectREFR::SetLocation(NiPoint3*), verified writes data.location
inline constexpr std::uintptr_t MoveToImpl = 0xA447F0;  // Address Library 1.6.1170 id 56626
inline constexpr std::size_t DataAngle = 0x48;          // TESObjectREFR::data.angle
inline constexpr std::size_t DataLocation = 0x54;       // TESObjectREFR::data.location
inline constexpr std::size_t ParentCell = 0x60;         // TESObjectREFR::parentCell
inline constexpr std::size_t CellFlags = 0x40;          // TESObjectCELL::cellFlags
inline constexpr std::uint16_t CellFlag_IsInterior = 1u << 0;
inline constexpr std::size_t CellWorldSpace = 0x128;    // TESObjectCELL::GetRuntimeData().worldSpace on 1.6.1170

}  // namespace refr

// ============================================================================
// TES forms.
// ============================================================================
namespace form {

inline constexpr std::uintptr_t LookupByID = 0x1E01A0;  // TESForm* LookupByID(FormID)
inline constexpr std::size_t    FormType   = 0x1A;      // uint8 form type

}  // namespace form

namespace data {

// CommonLibSSE-NG RE::Offset::TESDataHandler::Singleton,
// Address Library 1.6.1170 id 400269. This is a pointer to TESDataHandler*.
inline constexpr std::uintptr_t TESDataHandlerPtr = 0x20F6320;

// Minimal TESDataHandler / TESFile layout needed for resolving plugin-local
// FormIDs without linking CommonLib.
inline constexpr std::size_t TESDataHandler_CompiledFileCollection = 0xD70;  // TESFileCollection
inline constexpr std::size_t TESFileCollection_Files = 0x00;                 // BSTArray<TESFile*>
inline constexpr std::size_t TESFileCollection_SmallFiles = 0x18;            // BSTArray<TESFile*>
inline constexpr std::size_t TESFile_FileName = 0x58;                        // char[MAX_PATH]
inline constexpr std::size_t TESFile_RecordFlags = 0x438;                    // uint32
inline constexpr std::size_t TESFile_CompileIndex = 0x478;                   // uint8
inline constexpr std::size_t TESFile_SmallFileCompileIndex = 0x47A;          // uint16
inline constexpr std::uint32_t TESFile_RecordFlagSmallFile = 1u << 9;

}  // namespace data

namespace handle {

// Address Library 1.6.1170 id 12332. Kept separate from the older console-path
// ResolveRefHandle RVA above because the signatures differ.
inline constexpr std::uintptr_t LookupReferenceByHandle = 0x179710;

}  // namespace handle

namespace process {

// Address Library 1.6.1170 id 400315 (CommonLib RELOCATION_ID(514167, 400315)):
// ProcessLists** singleton used by RE::ProcessLists::GetSingleton().
inline constexpr std::uintptr_t ProcessListsPtr = 0x20F69B0;
inline constexpr std::size_t BSTArray_Data = 0x00;
inline constexpr std::size_t BSTArray_Capacity = 0x08;
inline constexpr std::size_t BSTArray_Size = 0x10;
inline constexpr std::size_t ProcessLists_HighActorHandles = 0x30;
inline constexpr std::size_t ProcessLists_LowActorHandles = 0x48;
inline constexpr std::size_t ProcessLists_MiddleHighActorHandles = 0x60;
inline constexpr std::size_t ProcessLists_MiddleLowActorHandles = 0x78;
inline constexpr std::size_t ProcessLists_MagicEffects = 0x108;
inline constexpr std::size_t ProcessLists_MagicEffectsLock = 0x120;
inline constexpr std::uint32_t MaxActorHandlesPerList = 4096;
inline constexpr std::uint32_t MaxMagicEffects = 16384;

}  // namespace process

namespace effect {

inline constexpr std::size_t ReferenceEffect_Target = 0x38;       // RE::ReferenceEffect::target ObjectRefHandle
inline constexpr std::size_t ReferenceEffect_Finished = 0x40;     // RE::ReferenceEffect::finished bool
inline constexpr std::size_t ShaderReferenceEffect_EffectData = 0x108;  // RE::ShaderReferenceEffect::effectData

}  // namespace effect

namespace crosshair {

// Address Library 1.6.1170 id 401585: CrosshairPickData** singleton.
// CrosshairPickData layout: target handle @+0x04, targetActor handle @+0x08.
inline constexpr std::uintptr_t CrosshairPickDataSingletonPtr = 0x3138ED0;
inline constexpr std::size_t CrosshairPickData_Target = 0x04;
inline constexpr std::size_t CrosshairPickData_TargetActor = 0x08;

}  // namespace crosshair

// ============================================================================
// Cell / world transition.
// ============================================================================
namespace cell {

// Loaded-cell batch transition:
//   FUN_140370f30 parses a destination, then calls FUN_14019ec20(DAT_1431872c8, flags).
//   FUN_14019ec20 sets the cell-loading flag, loads/centers the affected cells, calls
//   FUN_14019ed60 for each moved cell (logs "Moving to interior/exterior cell ..."),
//   then clears the loading flag. Hook after the original for one event-driven cleanup
//   per loaded-cell batch, not a heartbeat.
inline constexpr std::uintptr_t LoadedCellBatch = 0x19EC20;  // FUN_14019ec20

}  // namespace cell

// ============================================================================
// Papyrus VM.
// ============================================================================
namespace papyrus {

inline constexpr std::uintptr_t SkyrimVMSingletonPtr = 0x20FBA70;  // *(SkyrimVM**); impl smart ptr at +0x200
inline constexpr std::size_t    SkyrimVM_Impl        = 0x200;      // SkyrimVM::impl (BSScript::IVirtualMachine*)

}  // namespace papyrus

// ============================================================================
// Render / Scaleform img:// virtual images (used by HagUI's Model3D widget, Route A =
// reuse the engine's menu-3D renderer + bind its render target as an img:// image).
// Full RE notes + roadmap: HagLoader/docs/Model3D-RE.md.
// ============================================================================
namespace render {

// ID3D11Device* -- VERIFIED LIVE: vtable[5](+0x28)=CreateTexture2D lives in d3d11.dll.
inline constexpr std::uintptr_t D3D11DevicePtr = 0x3286A10;   // *(ID3D11Device**)
// ID3D11Device vtable byte-offsets we use (standard D3D11 layout):
inline constexpr std::uintptr_t kDev_CreateTexture2D        = 0x28;  // slot 5
inline constexpr std::uintptr_t kDev_CreateShaderResView    = 0x38;  // slot 7
inline constexpr std::uintptr_t kDev_CreateRenderTargetView = 0x48;  // slot 9

// img:// virtual-image registration (from the BGSUserIcon registrar FUN_140941970):
inline constexpr std::uintptr_t VImageCreate = 0xD2F140;  // FUN_140d2f140(BSFixedString* name) -> entry(0x58)
inline constexpr std::uintptr_t TextureCreate = 0xE48660; // FUN_140e48660(texMgr, w, h, pixels, usage, fmt, rtFlag)
inline constexpr std::uintptr_t VImageRegister = 0xFAF5E0;// FUN_140faf5e0(loader, entry) (loader singleton TBD)
inline constexpr std::size_t    VImage_Name = 0x20, VImage_Texture = 0x48;  // entry field offsets
inline constexpr std::uint32_t  kFmt_RGBA8 = 0x1c;        // format arg for TextureCreate

// Menu-3D preview machinery (Inventory item preview -- reuse for any model):
inline constexpr std::uintptr_t Inventory3DManagerPtr = 0x3187778;  // *(Inventory3DManager**)
inline constexpr std::uintptr_t Inv3D_UpdateEntry = 0x927850;  // FUN_140927850(mgr, {form,extra})
inline constexpr std::uintptr_t Inv3D_Update      = 0x927880;  // FUN_140927880(mgr, form, extra) load+attach
inline constexpr std::uintptr_t Inv3D_Clear       = 0x927C40;  // FUN_140927c40(mgr)
inline constexpr std::uintptr_t Inv3D_GetSceneNode= 0x928130;  // FUN_140928130(mgr) -> menu-3D scene node
inline constexpr std::uintptr_t MenuSceneSetup    = 0x643060;  // FUN_140643060 builds "3DMenu" node + RT

}  // namespace render

}  // namespace game
