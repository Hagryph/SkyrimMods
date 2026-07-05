#include "PCH.h"
#include "ipc/MainThread.h"

#include <future>
#include <memory>
#include <atomic>
#include <chrono>

namespace hag::mt {

namespace {
    skse::TaskInterface* g_task = nullptr;

    // Whether the SKSE main-thread task queue has been seen draining. It does NOT drain at the
    // main menu, so we start "idle": run work inline there (instant) instead of waiting. Once a
    // queued task actually runs (in-game) this flips true and we route to the main thread; if the
    // queue later goes idle again (back to the menu) the next wait times out and flips it back.
    std::atomic<bool> g_responsive{false};

    struct FnTask : skse::TaskDelegate { std::function<void()> fn; };
    void RunThunk(skse::TaskDelegate* t)     { static_cast<FnTask*>(t)->fn(); }
    void DisposeThunk(skse::TaskDelegate* t) { delete static_cast<FnTask*>(t); }
    const skse::TaskDelegate::VTbl kFnTaskVtbl = { &RunThunk, &DisposeThunk };
}  // namespace

void SetTaskInterface(skse::TaskInterface* t) { g_task = t; }
bool Available() { return g_task != nullptr; }

bool Run(std::function<void()> fn, unsigned timeoutMs) {
    auto guard  = std::make_shared<std::atomic<bool>>(false);
    auto onMain = std::make_shared<std::atomic<bool>>(false);
    auto prom   = std::make_shared<std::promise<void>>();
    auto fut    = prom->get_future();

    // Runs fn at most once (first claimant wins). A task that runs on the main thread also proves
    // the queue is live again, so it flips g_responsive back on even if it lost the claim race.
    auto doWork = [guard, fn, prom, onMain](bool mainThread) {
        if (mainThread) g_responsive.store(true);
        if (!guard->exchange(true)) {
            onMain->store(mainThread);
            try { fn(); } catch (...) {}
            prom->set_value();
        }
    };

    // Always queue a main-thread attempt — it's the recovery probe that re-detects "in-game".
    if (g_task) {
        auto* t = new FnTask();
        t->vtbl = &kFnTaskVtbl;
        t->fn   = [doWork] { doWork(true); };
        g_task->AddTask(t);
    }

    // If the main thread has been responsive, wait for it (safe execution). Otherwise (menu),
    // don't wait — run inline immediately.
    if (g_responsive.load()) {
        if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready) {
            return onMain->load();
        }
        g_responsive.store(false);   // queue went idle -> back to inline
    }
    doWork(false);
    fut.wait();
    return onMain->load();
}

bool RunOnMain(std::function<void()> fn, unsigned timeoutMs) {
    if (!g_task) return false;

    // state: 0 pending, 1 claimed-by-task (running/ran on main), 2 abandoned-by-waiter (won't run).
    auto state = std::make_shared<std::atomic<int>>(0);
    auto prom  = std::make_shared<std::promise<void>>();
    auto fut   = prom->get_future();

    auto* t = new FnTask();
    t->vtbl = &kFnTaskVtbl;
    t->fn = [state, fn, prom] {
        g_responsive.store(true);   // it drained -> the main thread is live
        int expected = 0;
        if (state->compare_exchange_strong(expected, 1)) {
            try { fn(); } catch (...) {}
            prom->set_value();
        }
        // lost to the waiter (abandoned): do nothing.
    };
    g_task->AddTask(t);

    if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready)
        return true;  // ran on the main thread

    // Timed out. Try to abandon before the task can claim it.
    int expected = 0;
    if (state->compare_exchange_strong(expected, 2)) {
        g_responsive.store(false);  // main thread not draining (menu / unfocused)
        return false;               // fn did NOT run and will NOT run
    }
    // The task claimed it in the race window — it's running now; wait for it to finish.
    fut.wait();
    return true;
}

}  // namespace hag::mt
