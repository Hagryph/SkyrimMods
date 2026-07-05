#include "PCH.h"
#include "ConsoleExec.h"
#include "Offsets.h"

namespace hag::console {

namespace {

using Alloc_t      = void* (*)(void*, std::size_t, std::size_t, int);
using Ctor1_t      = void* (*)(void*);
using Ctor2_t      = void  (*)(void*);
using CompileRun_t = void  (*)(void*, void*, int, void*);
using Dtor_t       = void  (*)(void*);

bool RunGuarded(const char* cmd, std::size_t len, Result* out,
                char* obuf, std::size_t ocap, std::size_t* olen) noexcept {
    *olen = 0;
    __try {
        void* heap = reinterpret_cast<void*>(offsets::FromRVA(offsets::kHeap));
        void* compiler = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kCompilerPtr));
        if (!compiler) {
            out->noCompiler = true;
            return true;
        }

        auto Alloc = reinterpret_cast<Alloc_t>(offsets::FromRVA(offsets::kAlloc));
        auto Ctor1 = reinterpret_cast<Ctor1_t>(offsets::FromRVA(offsets::kCtor1));
        auto Ctor2 = reinterpret_cast<Ctor2_t>(offsets::FromRVA(offsets::kCtor2));
        auto CompileRun = reinterpret_cast<CompileRun_t>(offsets::FromRVA(offsets::kCompileRun));
        auto Dtor = reinterpret_cast<Dtor_t>(offsets::FromRVA(offsets::kDtor));

        alignas(16) unsigned char script[0x100];
        std::memset(script, 0, sizeof(script));

        Ctor1(script);
        *reinterpret_cast<void**>(script) = reinterpret_cast<void*>(offsets::FromRVA(offsets::kScriptVtbl));
        script[offsets::kOffFormType] = 0x13;
        Ctor2(script);

        void* textBuf = Alloc(heap, len + 1, 0, 0);
        if (!textBuf) {
            Dtor(script);
            return true;
        }
        std::memcpy(textBuf, cmd, len);
        reinterpret_cast<char*>(textBuf)[len] = '\0';
        *reinterpret_cast<void**>(script + offsets::kOffText) = textBuf;

        unsigned lenBefore = 0;
        if (void* c0 = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kConsolePtr))) {
            lenBefore = *reinterpret_cast<unsigned short*>(reinterpret_cast<char*>(c0) + offsets::kConsLen);
        }

        CompileRun(script, compiler, 1, nullptr);

        out->compiledSize = *reinterpret_cast<unsigned*>(script + offsets::kOffCompiled);
        out->compiled = out->compiledSize > 0;

        if (void* c1 = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kConsolePtr))) {
            char* data = *reinterpret_cast<char**>(reinterpret_cast<char*>(c1) + offsets::kConsData);
            unsigned lenAfter = *reinterpret_cast<unsigned short*>(reinterpret_cast<char*>(c1) + offsets::kConsLen);
            unsigned start = (lenAfter >= lenBefore) ? lenBefore : 0u;
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
    if (command.empty()) return r;

    char obuf[8192];
    std::size_t olen = 0;
    if (!RunGuarded(command.c_str(), command.size(), &r, obuf, sizeof(obuf), &olen)) {
        r.faulted = true;
    } else if (olen) {
        r.output.assign(obuf, olen);
    }
    return r;
}

}  // namespace hag::console
