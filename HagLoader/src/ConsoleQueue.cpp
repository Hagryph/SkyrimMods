#include "PCH.h"
#include "ConsoleQueue.h"
#include "Log.h"
#include "UI/HagMenu.h"

#include <atomic>
#include <mutex>
#include <queue>

namespace hag::console_queue {

namespace {

skse::TaskInterface* g_task = nullptr;
std::mutex g_mutex;
std::queue<std::string> g_commands;
std::atomic<bool> g_scheduled{false};

struct DrainTask : skse::TaskDelegate {
};

void ScheduleDrain();

void RunDrain(skse::TaskDelegate* self) {
    for (;;) {
        std::string command;
        {
            std::lock_guard lock(g_mutex);
            if (g_commands.empty()) {
                g_scheduled.store(false);
                if (g_commands.empty()) return;
                if (g_scheduled.exchange(true)) return;
            }
            command = std::move(g_commands.front());
            g_commands.pop();
        }

        HAG_INFO("queued console command running on SKSE task thread: haguiOpen={} command='{}'",
                 ui::HagMenu::IsOpen(), command);
        console::Result result = console::Run(command);
        HAG_INFO("queued console command result: compiled={} faulted={} noCompiler={} bytes={} output='{}'",
                 result.compiled, result.faulted, result.noCompiler, result.compiledSize, result.output);
    }
}

void DisposeDrain(skse::TaskDelegate* self) {
    delete static_cast<DrainTask*>(self);
}

const skse::TaskDelegate::VTbl kDrainVtbl = { &RunDrain, &DisposeDrain };

void ScheduleDrain() {
    if (!g_task) {
        g_scheduled.store(false);
        HAG_WARN("cannot schedule queued console command: SKSE task interface is unavailable");
        return;
    }

    auto* task = new DrainTask();
    task->vtbl = &kDrainVtbl;
    g_task->AddTask(task);
}

}  // namespace

void SetTaskInterface(skse::TaskInterface* task) {
    g_task = task;
    HAG_INFO("SKSE task interface {}", task ? "acquired for deferred console commands" : "unavailable");
}

bool Available() {
    return g_task != nullptr;
}

bool Queue(std::string command) {
    if (command.empty() || !g_task) return false;
    {
        std::lock_guard lock(g_mutex);
        g_commands.push(std::move(command));
    }

    HAG_INFO("queued console command onto SKSE task queue; haguiOpen={}", ui::HagMenu::IsOpen());

    if (!g_scheduled.exchange(true)) {
        ScheduleDrain();
    }
    return true;
}

}  // namespace hag::console_queue
