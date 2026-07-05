#include "PCH.h"
#include "NativeTaskQueue.h"

#include "GameState.h"
#include "Log.h"

#include <atomic>
#include <mutex>
#include <queue>

namespace hag::native_task_queue {

namespace {

skse::TaskInterface* g_task = nullptr;
std::mutex g_mutex;
std::atomic<bool> g_scheduled{false};

struct QueuedTask {
    HagLoader_MainThreadTaskCb callback = nullptr;
    void* user = nullptr;
};

std::queue<QueuedTask> g_tasks;

struct DrainTask : skse::TaskDelegate {
};

bool HasQueuedTasks() {
    std::lock_guard lock(g_mutex);
    return !g_tasks.empty();
}

void ScheduleDrain();

void RunQueuedTaskGuarded(HagLoader_MainThreadTaskCb callback, void* user) noexcept {
    __try {
        callback(user);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        HAG_ERR("queued native task faulted");
    }
}

void TryScheduleDrain(const char* reason) {
    if (!HasQueuedTasks()) return;
    if (!game_state::IsGameRunning()) {
        HAG_INFO("queued native task drain waiting: game is not running ({})",
                 reason ? reason : "unspecified");
        return;
    }
    if (!g_scheduled.exchange(true)) {
        HAG_INFO("queued native task drain scheduled ({})", reason ? reason : "unspecified");
        ScheduleDrain();
    }
}

void RunDrain(skse::TaskDelegate*) {
    for (;;) {
        if (!game_state::IsGameRunning()) {
            HAG_INFO("queued native task drain paused: game is not running");
            g_scheduled.store(false);
            return;
        }

        QueuedTask job;
        {
            std::lock_guard lock(g_mutex);
            if (g_tasks.empty()) {
                g_scheduled.store(false);
                return;
            }
            job = g_tasks.front();
            g_tasks.pop();
        }

        if (!job.callback) continue;

        HAG_INFO("queued native task running on SKSE task thread");
        RunQueuedTaskGuarded(job.callback, job.user);
    }
}

void DisposeDrain(skse::TaskDelegate* self) {
    delete static_cast<DrainTask*>(self);
}

const skse::TaskDelegate::VTbl kDrainVtbl = {&RunDrain, &DisposeDrain};

void ScheduleDrain() {
    if (!g_task) {
        g_scheduled.store(false);
        HAG_WARN("cannot schedule queued native task: SKSE task interface is unavailable");
        return;
    }

    auto* task = new DrainTask();
    task->vtbl = &kDrainVtbl;
    g_task->AddTask(task);
}

}  // namespace

void SetTaskInterface(skse::TaskInterface* task) {
    static std::once_flag registerStateCallback;
    g_task = task;
    std::call_once(registerStateCallback, [] {
        game_state::AddChangeCallback(
            [](bool running, void*) { native_task_queue::OnGameRunningChanged(running); });
    });
    HAG_INFO("SKSE task interface {}", task ? "acquired for deferred native tasks" : "unavailable");
}

bool Queue(HagLoader_MainThreadTaskCb callback, void* user) {
    if (!callback || !g_task) return false;
    {
        std::lock_guard lock(g_mutex);
        g_tasks.push({callback, user});
    }

    HAG_INFO("queued native task; gameRunning={}", game_state::IsGameRunning());
    TryScheduleDrain("new native task");
    return true;
}

void OnGameRunningChanged(bool running) {
    if (!running) return;
    TryScheduleDrain("game resumed");
}

}  // namespace hag::native_task_queue
