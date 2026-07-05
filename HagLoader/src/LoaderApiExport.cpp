#include "PCH.h"
#include "ConsoleQueue.h"
#include "HagLoaderAPI.h"
#include "Log.h"

namespace {

bool C_QueueConsoleCommand(const char* command) {
    if (!command || !*command) return false;
    const bool queued = hag::console_queue::Queue(command);
    HAG_INFO("HagLoader API QueueConsoleCommand('{}') {}", command, queued ? "queued" : "queue failed");
    return queued;
}

const HagLoaderAPI g_loaderApi = {
    HAGLOADER_ABI_VERSION,
    &C_QueueConsoleCommand,
};

}  // namespace

extern "C" __declspec(dllexport) const HagLoaderAPI* HagLoader_GetAPI(std::uint32_t abiVersion) {
    return (abiVersion >= 1 && abiVersion <= HAGLOADER_ABI_VERSION) ? &g_loaderApi : nullptr;
}
