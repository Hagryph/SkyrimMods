#pragma once
#include "PCH.h"
#include <string>

// Run an in-game console command line from outside the game, by reproducing exactly what the
// game's own console does: build a transient Script form, set its command text, CompileAndRun it,
// then tear it down. Recipe reverse-engineered and confirmed live via HagIPC hot-load (1.6.1170).
//
// MUST be called on the game main thread (it registers a form and drives the script VM) -- the
// Server marshals via mt::RunOnMain, which refuses when the main thread isn't draining.
namespace hag::console {

struct Result {
    bool        faulted    = false;  // an access violation was caught (SEH)
    bool        noCompiler = false;  // the global ScriptCompiler wasn't available yet
    bool        compiled   = false;  // the command produced bytecode (compiledSize > 0)
    unsigned    compiledSize = 0;    // Script+0x28 after CompileAndRun
    std::string output;              // text the command echoed to the console (from console+0x408)
};

Result Run(const std::string& command);

}  // namespace hag::console
