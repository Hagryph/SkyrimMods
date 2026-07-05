#include "PCH.h"
#include "ipc/ConsoleExec.h"
#include "Offsets.h"

#include <cstring>

// SEH must live in its own translation unit with POD-only locals inside __try (no C++ objects that
// require unwinding) -- same discipline as CallExec.cpp / MemAccess.cpp.
namespace hag::console {

namespace {

// Offset names (kAlloc, kCtor1, ...) come from Offsets.h, which re-exports the shared table.
using namespace hag::offsets;

using Alloc_t      = void* (*)(void*, std::size_t, std::size_t, int);
using Ctor1_t      = void* (*)(void*);
using Ctor2_t      = void  (*)(void*);
using CompileRun_t = void  (*)(void*, void*, int, void*);
using Dtor_t       = void  (*)(void*);

// Everything that touches game memory/functions runs here, guarded. Returns false on SEH fault.
// obuf/ocap/olen receive the console text the command printed (POD out-params; the std::string is
// built by the caller outside the __try).
bool RunGuarded(const char* cmd, std::size_t len, Result* out,
                char* obuf, std::size_t ocap, std::size_t* olen) noexcept {
    *olen = 0;
    __try {
        void* heap     = reinterpret_cast<void*>(offsets::FromRVA(kHeap));
        void* compiler = *reinterpret_cast<void**>(offsets::FromRVA(kCompilerPtr));
        if (!compiler) { out->noCompiler = true; return true; }

        auto Alloc      = reinterpret_cast<Alloc_t>(offsets::FromRVA(kAlloc));
        auto Ctor1      = reinterpret_cast<Ctor1_t>(offsets::FromRVA(kCtor1));
        auto Ctor2      = reinterpret_cast<Ctor2_t>(offsets::FromRVA(kCtor2));
        auto CompileRun = reinterpret_cast<CompileRun_t>(offsets::FromRVA(kCompileRun));
        auto Dtor       = reinterpret_cast<Dtor_t>(offsets::FromRVA(kDtor));

        // Transient Script on a zeroed buffer (the game builds its console Script on the stack).
        alignas(16) unsigned char script[0x100];
        std::memset(script, 0, sizeof(script));

        Ctor1(script);                                                                  // base ctor
        *reinterpret_cast<void**>(script) = reinterpret_cast<void*>(offsets::FromRVA(kScriptVtbl));
        script[kOffFormType] = 0x13;                                                    // FormType::Script
        Ctor2(script);                                                                  // derived ctor

        // Command text into a game-heap buffer (the dtor frees Script+0x38 via the game allocator).
        void* textBuf = Alloc(heap, len + 1, 0, 0);
        if (!textBuf) { Dtor(script); return true; }  // compiled stays false
        std::memcpy(textBuf, cmd, len);
        reinterpret_cast<char*>(textBuf)[len] = '\0';
        *reinterpret_cast<void**>(script + kOffText) = textBuf;

        // Snapshot the console output length so we can copy back only what THIS command appends.
        unsigned lenBefore = 0;
        if (void* c0 = *reinterpret_cast<void**>(offsets::FromRVA(kConsolePtr)))
            lenBefore = *reinterpret_cast<unsigned short*>(reinterpret_cast<char*>(c0) + kConsLen);

        // refr = null: a socket client has no "selected ref"; player.* / global commands resolve on their own.
        CompileRun(script, compiler, 1, nullptr);

        out->compiledSize = *reinterpret_cast<unsigned*>(script + kOffCompiled);
        out->compiled     = out->compiledSize > 0;

        // Copy whatever the command appended to the console buffer (its echo / results).
        if (void* c1 = *reinterpret_cast<void**>(offsets::FromRVA(kConsolePtr))) {
            char*    data     = *reinterpret_cast<char**>(reinterpret_cast<char*>(c1) + kConsData);
            unsigned lenAfter = *reinterpret_cast<unsigned short*>(reinterpret_cast<char*>(c1) + kConsLen);
            unsigned start    = (lenAfter >= lenBefore) ? lenBefore : 0u;  // buffer may have been flushed
            if (data && lenAfter > start) {
                std::size_t n = static_cast<std::size_t>(lenAfter - start);
                if (n > ocap) n = ocap;
                std::memcpy(obuf, data + start, n);
                *olen = n;
            }
        }

        Dtor(script);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

Result Run(const std::string& command) {
    Result r{};
    if (command.empty()) return r;  // compiled == false
    char        obuf[8192];
    std::size_t olen = 0;
    if (!RunGuarded(command.c_str(), command.size(), &r, obuf, sizeof(obuf), &olen))
        r.faulted = true;
    else if (olen)
        r.output.assign(obuf, olen);
    return r;
}

}  // namespace hag::console
