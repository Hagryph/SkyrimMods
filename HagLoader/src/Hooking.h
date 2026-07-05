#pragma once
#include <cstdint>
#include <MinHook.h>

namespace hag {

// Thin OOP wrapper over MinHook for absolute-address trampoline hooks.
class Hooking {
public:
    static bool Init();    // MH_Initialize
    static bool Commit();  // MH_EnableHook(MH_ALL_HOOKS)

    // Install a trampoline at an absolute runtime address.
    // `original` receives the forwarder used to call the un-hooked function.
    template <class F>
    static bool Create(std::uintptr_t target, F detour, F& original) {
        return MH_CreateHook(reinterpret_cast<LPVOID>(target),
                             reinterpret_cast<LPVOID>(detour),
                             reinterpret_cast<LPVOID*>(&original)) == MH_OK;
    }
};

}  // namespace hag
