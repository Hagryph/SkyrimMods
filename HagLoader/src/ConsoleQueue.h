#pragma once

#include "PCH.h"
#include "ConsoleExec.h"
#include "SKSE_Min.h"

namespace hag::console_queue {

void SetTaskInterface(skse::TaskInterface* task);
bool Available();
bool Queue(std::string command);
bool ShouldDeferConsoleCommands();

}  // namespace hag::console_queue
