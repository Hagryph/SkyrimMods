#include "PCH.h"
#include "ConsoleExec.h"
#include "ConsoleQueue.h"
#include "HagLoaderAPI.h"
#include "Log.h"

namespace {

thread_local std::string g_consoleOutput;

bool C_RunConsoleCommand(const char* command, HagLoader_ConsoleResult* out) {
    if (!command || !*command) return false;

    if (hag::console_queue::Available()) {
        const bool queued = hag::console_queue::Queue(command);
        g_consoleOutput = queued ? "queued on SKSE main-thread task queue" :
                                   "failed to queue command";
        if (out) {
            out->faulted = false;
            out->noCompiler = false;
            out->compiled = false;
            out->compiledSize = 0;
            out->output = g_consoleOutput.c_str();
        }
        HAG_INFO("HagLoader API RunConsoleCommand('{}') {}", command, queued ? "queued" : "queue failed");
        return queued;
    }

    hag::console::Result r = hag::console::Run(command);
    g_consoleOutput = r.output;

    if (out) {
        out->faulted = r.faulted;
        out->noCompiler = r.noCompiler;
        out->compiled = r.compiled;
        out->compiledSize = r.compiledSize;
        out->output = g_consoleOutput.c_str();
    }

    HAG_INFO("HagLoader API RunConsoleCommand('{}') compiled={} faulted={} noCompiler={}",
             command, r.compiled, r.faulted, r.noCompiler);
    return !r.faulted && !r.noCompiler && r.compiled;
}

const HagLoaderAPI g_loaderApi = {
    HAGLOADER_ABI_VERSION,
    &C_RunConsoleCommand,
};

}  // namespace

extern "C" __declspec(dllexport) const HagLoaderAPI* HagLoader_GetAPI(std::uint32_t abiVersion) {
    return (abiVersion >= 1 && abiVersion <= HAGLOADER_ABI_VERSION) ? &g_loaderApi : nullptr;
}
