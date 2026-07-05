#pragma once

#include "PCH.h"
#include "ConsoleExec.h"
#include "HagLoaderAPI.h"
#include "SKSE_Min.h"

namespace hag::console_queue {

void SetTaskInterface(skse::TaskInterface* task);
bool Available();
bool Queue(std::string command, HagLoader_ConsoleResultCb callback = nullptr, void* user = nullptr);

}  // namespace hag::console_queue
