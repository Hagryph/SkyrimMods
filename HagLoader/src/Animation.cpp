#include "PCH.h"
#include "Animation.h"

#include "GameOffsets.h"
#include "Log.h"
#include "NativeTaskQueue.h"
#include "Offsets.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace hag::animation {

namespace {

struct AutoStopContext {
    HANDLE timer = nullptr;
    std::uint32_t actorFormID = 0;
    std::uint32_t stopIdleFormID = 0;
    std::uint32_t generation = 0;
};

std::mutex g_generationMutex;
std::unordered_map<std::uint32_t, std::uint32_t> g_actorStopGeneration;

void* LookupFormByIDGuarded(std::uint32_t formID) noexcept {
    if (formID == 0) return nullptr;
    __try {
        using LookupByIdFn = void* (*)(std::uint32_t);
        auto lookup = reinterpret_cast<LookupByIdFn>(offsets::FromRVA(game::form::LookupByID));
        return lookup ? lookup(formID) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

std::uint32_t NextStopGeneration(std::uint32_t actorFormID) {
    std::lock_guard lock(g_generationMutex);
    return ++g_actorStopGeneration[actorFormID];
}

std::uint32_t CurrentStopGeneration(std::uint32_t actorFormID) {
    std::lock_guard lock(g_generationMutex);
    const auto it = g_actorStopGeneration.find(actorFormID);
    return it == g_actorStopGeneration.end() ? 0 : it->second;
}

bool PlayIdleWithTargetRawGuarded(void* actor, void* idle, void* targetRef) noexcept {
    if (!actor || !idle) return false;

    __try {
        void* process = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(actor) + game::actor::ActorProcessOffset);
        if (!process) return false;

        using SetupSpecialIdleFn = bool (*)(void*,
                                            void*,
                                            std::uint32_t,
                                            void*,
                                            bool,
                                            bool,
                                            void*);
        auto setupSpecialIdle = reinterpret_cast<SetupSpecialIdleFn>(
            offsets::FromRVA(game::actor::AIProcess_SetupSpecialIdle));
        return setupSpecialIdle(process,
                                actor,
                                game::actor::DefaultObject_ActionIdle,
                                idle,
                                true,
                                false,
                                targetRef);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool StopIdleInternal(std::uint32_t actorFormID, std::uint32_t stopIdleFormID, const char* reason) {
    void* actor = LookupFormByIDGuarded(actorFormID);
    void* stopIdle = LookupFormByIDGuarded(stopIdleFormID);
    if (!actor || !stopIdle) {
        HAG_WARN("animation idle stop failed: actor={:#x} actorFound={} stopIdle={:#x} stopFound={} reason={}",
                 actorFormID,
                 actor != nullptr,
                 stopIdleFormID,
                 stopIdle != nullptr,
                 reason ? reason : "unspecified");
        return false;
    }

    const bool stopped = PlayIdleWithTargetRawGuarded(actor, stopIdle, nullptr);
    HAG_INFO("animation idle stop: actor={:#x} stopIdle={:#x} stopped={} reason={}",
             actorFormID,
             stopIdleFormID,
             stopped,
             reason ? reason : "unspecified");
    return stopped;
}

void AutoStopTask(void* user);

void CALLBACK AutoStopTimerProc(PVOID user, BOOLEAN) {
    auto* ctx = static_cast<AutoStopContext*>(user);
    if (!ctx) return;

    const HANDLE timer = ctx->timer;
    ctx->timer = nullptr;
    if (timer) {
        ::DeleteTimerQueueTimer(nullptr, timer, nullptr);
    }

    if (native_task_queue::Queue(&AutoStopTask, ctx)) {
        return;
    }

    HAG_WARN("animation auto-stop could not be queued: actor={:#x} stopIdle={:#x}",
             ctx->actorFormID,
             ctx->stopIdleFormID);
    delete ctx;
}

bool ScheduleAutoStop(std::uint32_t actorFormID,
                      std::uint32_t stopIdleFormID,
                      std::uint32_t stopDelayMs,
                      std::uint32_t generation) {
    if (actorFormID == 0 || stopIdleFormID == 0 || stopDelayMs == 0) return true;

    auto* ctx = new AutoStopContext();
    ctx->actorFormID = actorFormID;
    ctx->stopIdleFormID = stopIdleFormID;
    ctx->generation = generation;

    HANDLE timer = nullptr;
    if (!::CreateTimerQueueTimer(&timer,
                                 nullptr,
                                 &AutoStopTimerProc,
                                 ctx,
                                 stopDelayMs,
                                 0,
                                 WT_EXECUTEDEFAULT)) {
        HAG_ERR("animation auto-stop timer failed: actor={:#x} stopIdle={:#x} delayMs={}",
                actorFormID,
                stopIdleFormID,
                stopDelayMs);
        delete ctx;
        return false;
    }

    ctx->timer = timer;
    HAG_INFO("animation auto-stop scheduled: actor={:#x} stopIdle={:#x} delayMs={} generation={}",
             actorFormID,
             stopIdleFormID,
             stopDelayMs,
             generation);
    return true;
}

void AutoStopTask(void* user) {
    std::unique_ptr<AutoStopContext> ctx(static_cast<AutoStopContext*>(user));
    if (!ctx) return;

    const auto current = CurrentStopGeneration(ctx->actorFormID);
    if (ctx->generation != current) {
        HAG_INFO("animation stale auto-stop ignored: actor={:#x} generation={} current={}",
                 ctx->actorFormID,
                 ctx->generation,
                 current);
        return;
    }

    StopIdleInternal(ctx->actorFormID, ctx->stopIdleFormID, "auto-stop");
}

}  // namespace

bool PlayIdleWithTargetAutoStop(std::uint32_t actorFormID,
                                std::uint32_t idleFormID,
                                std::uint32_t targetFormID,
                                std::uint32_t stopIdleFormID,
                                std::uint32_t stopDelayMs) {
    if (actorFormID == 0 || idleFormID == 0) return false;

    void* actor = LookupFormByIDGuarded(actorFormID);
    void* idle = LookupFormByIDGuarded(idleFormID);
    void* target = targetFormID != 0 ? LookupFormByIDGuarded(targetFormID) : nullptr;
    if (!actor || !idle || (targetFormID != 0 && !target)) {
        HAG_WARN("animation idle play failed: actor={:#x} actorFound={} idle={:#x} idleFound={} target={:#x} targetFound={}",
                 actorFormID,
                 actor != nullptr,
                 idleFormID,
                 idle != nullptr,
                 targetFormID,
                 targetFormID == 0 || target != nullptr);
        return false;
    }

    const auto generation = NextStopGeneration(actorFormID);
    if (!PlayIdleWithTargetRawGuarded(actor, idle, target)) {
        HAG_WARN("animation idle play rejected: actor={:#x} idle={:#x} target={:#x}",
                 actorFormID,
                 idleFormID,
                 targetFormID);
        return false;
    }

    if (!ScheduleAutoStop(actorFormID, stopIdleFormID, stopDelayMs, generation)) {
        StopIdleInternal(actorFormID, stopIdleFormID, "auto-stop scheduling failed");
        return false;
    }

    HAG_INFO("animation idle play started: actor={:#x} idle={:#x} target={:#x} stopIdle={:#x} stopDelayMs={} generation={}",
             actorFormID,
             idleFormID,
             targetFormID,
             stopIdleFormID,
             stopDelayMs,
             generation);
    return true;
}

bool StopIdle(std::uint32_t actorFormID, std::uint32_t stopIdleFormID) {
    if (actorFormID == 0 || stopIdleFormID == 0) return false;
    NextStopGeneration(actorFormID);
    return StopIdleInternal(actorFormID, stopIdleFormID, "explicit stop");
}

}  // namespace hag::animation
