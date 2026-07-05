#include "PCH.h"
#include "PapyrusCall.h"

#include "GameState.h"
#include "Log.h"
#include "Offsets.h"

#include <atomic>
#include <mutex>
#include <new>
#include <queue>

namespace hag::papyrus_call {

namespace {

constexpr std::size_t kSkyrimVMImplOffset = offsets::kSkyrimVM_Impl;
constexpr std::size_t kVSlotDispatchStaticCall = 0x26;

skse::TaskInterface* g_task = nullptr;
std::mutex g_mutex;
std::atomic<bool> g_scheduled{false};

struct QueuedStaticCall {
    std::string scriptName;
    std::string functionName;
    HagLoader_PapyrusResultCb callback = nullptr;
    void* user = nullptr;
};

struct DispatchResult {
    bool faulted = false;
    bool dispatched = false;
    std::string message;
};

std::queue<QueuedStaticCall> g_calls;

struct FixedString {
    const char* data = nullptr;

    explicit FixedString(const char* text) {
        reinterpret_cast<void (*)(FixedString*, const char*)>(
            offsets::FromRVA(offsets::kBSFixedString_ctor))(this, text ? text : "");
    }

    ~FixedString() {
        reinterpret_cast<void (*)(FixedString*)>(
            offsets::FromRVA(offsets::kBSFixedString_dtor))(this);
    }

    FixedString(const FixedString&) = delete;
    FixedString& operator=(const FixedString&) = delete;
};
static_assert(sizeof(FixedString) == 0x8);

struct FixedStringRaw {
    const char* data = nullptr;
};
static_assert(sizeof(FixedStringRaw) == 0x8);

// Enough of BSScrapArray<Variable> for ZeroFunctionArguments. The VM allocates
// and owns the array; a zero-arg call only needs to leave it empty.
struct ScrapVariableArray {
    void* allocator = nullptr;
    void* data = nullptr;
    std::uint32_t capacity = 0;
    std::uint32_t pad14 = 0;
    std::uint32_t size = 0;
    std::uint32_t pad1C = 0;
};
static_assert(sizeof(ScrapVariableArray) == 0x20);

struct ZeroFunctionArguments {
    struct VTbl {
        void (*Dtor)(ZeroFunctionArguments* self, std::uint32_t deleting);
        bool (*Call)(ZeroFunctionArguments* self, ScrapVariableArray& dst);
    };

    const VTbl* vtbl = nullptr;
};

void ZeroDtor(ZeroFunctionArguments* self, std::uint32_t deleting) {
    if ((deleting & 1u) != 0u) {
        delete self;
    }
}

bool ZeroCall(ZeroFunctionArguments*, ScrapVariableArray& dst) {
    dst.size = 0;
    return true;
}

const ZeroFunctionArguments::VTbl kZeroArgsVtbl = {&ZeroDtor, &ZeroCall};

struct DrainTask : skse::TaskDelegate {
};

using DispatchStaticCall_t = bool (*)(void* vm,
                                      const FixedStringRaw& scriptName,
                                      const FixedStringRaw& functionName,
                                      ZeroFunctionArguments* args,
                                      void* resultCallbackSmartPtr);

void* GetVirtualMachine() {
    auto* skyrimVM = *reinterpret_cast<std::byte**>(offsets::FromRVA(offsets::kSkyrimVM_ptr));
    if (!skyrimVM) return nullptr;
    return *reinterpret_cast<void**>(skyrimVM + kSkyrimVMImplOffset);
}

bool DispatchStaticNoArgsGuarded(const char* scriptName, const char* functionName, bool* dispatched) noexcept {
    if (dispatched) *dispatched = false;
    __try {
        void* vm = GetVirtualMachine();
        if (!vm) {
            return true;
        }

        FixedStringRaw script;
        FixedStringRaw function;
        reinterpret_cast<void (*)(FixedStringRaw*, const char*)>(
            offsets::FromRVA(offsets::kBSFixedString_ctor))(&script, scriptName ? scriptName : "");
        reinterpret_cast<void (*)(FixedStringRaw*, const char*)>(
            offsets::FromRVA(offsets::kBSFixedString_ctor))(&function, functionName ? functionName : "");

        auto* args = new (std::nothrow) ZeroFunctionArguments();
        if (!args) {
            reinterpret_cast<void (*)(FixedStringRaw*)>(
                offsets::FromRVA(offsets::kBSFixedString_dtor))(&function);
            reinterpret_cast<void (*)(FixedStringRaw*)>(
                offsets::FromRVA(offsets::kBSFixedString_dtor))(&script);
            return true;
        }
        args->vtbl = &kZeroArgsVtbl;

        void* callback = nullptr;
        auto** vtbl = *reinterpret_cast<void***>(vm);
        auto dispatch = reinterpret_cast<DispatchStaticCall_t>(vtbl[kVSlotDispatchStaticCall]);
        const bool ok = dispatch(vm, script, function, args, &callback);

        reinterpret_cast<void (*)(FixedStringRaw*)>(
            offsets::FromRVA(offsets::kBSFixedString_dtor))(&function);
        reinterpret_cast<void (*)(FixedStringRaw*)>(
            offsets::FromRVA(offsets::kBSFixedString_dtor))(&script);
        if (dispatched) *dispatched = ok;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DispatchResult DispatchStaticNoArgs(const std::string& scriptName, const std::string& functionName) noexcept {
    DispatchResult result;
    if (!GetVirtualMachine()) {
        result.message = "SkyrimVM unavailable";
        return result;
    }

    bool dispatched = false;
    if (!DispatchStaticNoArgsGuarded(scriptName.c_str(), functionName.c_str(), &dispatched)) {
        result.faulted = true;
        result.message = "Papyrus static call faulted";
        return result;
    }

    result.dispatched = dispatched;
    result.message = result.dispatched ? "Papyrus static call dispatched" : "Papyrus static call rejected by VM";
    return result;
}

bool HasQueuedCalls() {
    std::lock_guard lock(g_mutex);
    return !g_calls.empty();
}

void ScheduleDrain();

void TryScheduleDrain(const char* reason) {
    if (!HasQueuedCalls()) return;
    if (!game_state::IsGameRunning()) {
        HAG_INFO("queued Papyrus call drain waiting: game is not running ({})",
                 reason ? reason : "unspecified");
        return;
    }
    if (!g_scheduled.exchange(true)) {
        HAG_INFO("queued Papyrus call drain scheduled ({})", reason ? reason : "unspecified");
        ScheduleDrain();
    }
}

void RunDrain(skse::TaskDelegate*) {
    for (;;) {
        if (!game_state::IsGameRunning()) {
            HAG_INFO("queued Papyrus call drain paused: game is not running");
            g_scheduled.store(false);
            return;
        }

        QueuedStaticCall job;
        {
            std::lock_guard lock(g_mutex);
            if (g_calls.empty()) {
                g_scheduled.store(false);
                return;
            }
            job = std::move(g_calls.front());
            g_calls.pop();
        }

        HAG_INFO("queued Papyrus static call running: {}.{}()",
                 job.scriptName, job.functionName);
        DispatchResult result = DispatchStaticNoArgs(job.scriptName, job.functionName);
        HAG_INFO("queued Papyrus static call result: dispatched={} faulted={} message='{}'",
                 result.dispatched, result.faulted, result.message);

        if (job.callback) {
            HagLoader_PapyrusResult out{};
            out.faulted = result.faulted;
            out.dispatched = result.dispatched;
            out.message = result.message.c_str();
            job.callback(job.user, &out);
        }
    }
}

void DisposeDrain(skse::TaskDelegate* self) {
    delete static_cast<DrainTask*>(self);
}

const skse::TaskDelegate::VTbl kDrainVtbl = {&RunDrain, &DisposeDrain};

void ScheduleDrain() {
    if (!g_task) {
        g_scheduled.store(false);
        HAG_WARN("cannot schedule queued Papyrus call: SKSE task interface is unavailable");
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
            [](bool running, void*) { papyrus_call::OnGameRunningChanged(running); });
    });
    HAG_INFO("SKSE task interface {}", task ? "acquired for deferred Papyrus calls" : "unavailable");
}

bool Available() {
    return g_task != nullptr;
}

bool QueueStaticCall(std::string scriptName,
                     std::string functionName,
                     HagLoader_PapyrusResultCb callback,
                     void* user) {
    if (scriptName.empty() || functionName.empty() || !g_task) return false;
    {
        std::lock_guard lock(g_mutex);
        g_calls.push({std::move(scriptName), std::move(functionName), callback, user});
    }

    HAG_INFO("queued Papyrus static call; gameRunning={}", game_state::IsGameRunning());
    TryScheduleDrain("new Papyrus call");
    return true;
}

void OnGameRunningChanged(bool running) {
    if (!running) return;
    TryScheduleDrain("game resumed");
}

}  // namespace hag::papyrus_call
