#pragma once

#include "HagLoaderAPI.h"
#include "SKSE_Min.h"

namespace hag::native_task_queue {

void SetTaskInterface(skse::TaskInterface* task);
bool Queue(HagLoader_MainThreadTaskCb callback, void* user);
void OnGameRunningChanged(bool running);

}  // namespace hag::native_task_queue
