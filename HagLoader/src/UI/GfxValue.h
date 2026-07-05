#pragma once
#include "PCH.h"
#include "Offsets.h"
#include <cstring>

// Shared Scaleform GFxMovieView value helpers — the exact ABI proven live in
// GfxInject.cpp (docs/UI-RE.md §10a), factored out so the option-page renderer
// (OptionRender.cpp) can drive HagUI's OWN movie the same way GfxInject drives
// the journal movie. Call as (*(*(void***)movie))[slot/8](movie, ...).
namespace hag::ui::gfx {

// GFxValue (0x18). MUST be zeroed before any Create*/GetVariable (they release the
// prior value first, so uninitialised bytes make them free garbage).
struct GFxValue { void* objIface; std::uint32_t type; std::uint32_t typeHi; std::uint64_t value; };
static_assert(sizeof(GFxValue) == 0x18, "GFxValue must be 0x18");

// GFxValue::ValueType tags we use.
enum : std::uint32_t { VT_Boolean = 2, VT_Number = 3, VT_String = 4 };

// GFxFunctionHandler::Params (recovered from FUN_140fac280); the handler is vtable[1].
struct FnParams { GFxValue* ret; void* movie; GFxValue* self; GFxValue* argsThis; GFxValue* args; int argc; int pad; void* userData; };

// A native GFxFunctionHandler: { vtable; refCount; }. Call is vtable slot 1.
struct NativeFn { void** vtbl; std::int32_t refCount; std::int32_t pad; };

inline void* Slot(void* m, std::uintptr_t off) { return (*reinterpret_cast<void***>(m))[off / 8]; }

inline bool MIsAvail(void* m, const char* p) {
    return reinterpret_cast<char(*)(void*, const char*)>(Slot(m, offsets::kMovie_IsAvailable))(m, p) != 0;
}
inline void MCreateStr(void* m, GFxValue* o, const char* s) {
    *o = {}; reinterpret_cast<void(*)(void*, GFxValue*, const char*)>(Slot(m, offsets::kMovie_CreateString))(m, o, s);
}
inline void MCreateFn(void* m, GFxValue* o, void* h) {
    *o = {}; reinterpret_cast<void(*)(void*, GFxValue*, void*, void*)>(Slot(m, offsets::kMovie_CreateFunction))(m, o, h, nullptr);
}
inline void MGetVar(void* m, GFxValue* o, const char* p) {
    *o = {}; reinterpret_cast<void(*)(void*, GFxValue*, const char*)>(Slot(m, offsets::kMovie_GetVariable))(m, o, p);
}
inline char MSetVar(void* m, const char* p, GFxValue* v) {
    return reinterpret_cast<char(*)(void*, const char*, GFxValue*, int)>(Slot(m, offsets::kMovie_SetVariable))(m, p, v, 0);
}
inline void MSetNum(void* m, const char* p, double d) {
    GFxValue v{}; v.type = VT_Number; std::memcpy(&v.value, &d, sizeof d); MSetVar(m, p, &v);
}
inline void MSetStr(void* m, const char* p, const char* s) {
    GFxValue sv{}; MCreateStr(m, &sv, s); MSetVar(m, p, &sv);
}
inline void MInvoke(void* m, const char* p) {
    reinterpret_cast<char(*)(void*, const char*, void*)>(offsets::FromRVA(offsets::kGFxMovie_Invoke))(m, p, nullptr);
}
inline double NumOf(const GFxValue& g) { double d = 0; std::memcpy(&d, &g.value, sizeof d); return d; }

}  // namespace hag::ui::gfx
