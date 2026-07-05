#pragma once

#include <cstdint>

namespace hag::hotkeys {

using Callback = void (*)(void* user);

bool RegisterForModule(HMODULE module, const char* name, std::int32_t vkCode, Callback callback, void* user);
bool SetForModule(HMODULE module, const char* name, std::int32_t vkCode);
bool InstallWindowHook();

}  // namespace hag::hotkeys
