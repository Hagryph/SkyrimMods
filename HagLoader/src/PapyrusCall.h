#pragma once

#include "PCH.h"
#include "HagLoaderAPI.h"
#include "SKSE_Min.h"

namespace hag::papyrus_call {

void SetTaskInterface(skse::TaskInterface* task);
bool Available();
bool QueueStaticCall(std::string scriptName,
                     std::string functionName,
                     HagLoader_PapyrusResultCb callback = nullptr,
                     void* user = nullptr);
void OnGameRunningChanged(bool running);

}  // namespace hag::papyrus_call
