#pragma once

#include "HagLoaderAPI.h"

namespace hag::cell_change {

void InstallHooks();
bool RegisterCallback(void* module, HagLoader_CellChangeCb callback, void* user);

}  // namespace hag::cell_change
