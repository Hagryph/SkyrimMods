#pragma once

namespace hag::game_state {

using ChangeCallback = void (*)(bool running, void* user);

bool IsGameRunning();
void SetGameRunning(bool running, const char* reason);
void RefreshFromUI(void* ui, const char* reason);
void AddChangeCallback(ChangeCallback callback, void* user = nullptr);
void InstallHooks();

}  // namespace hag::game_state
