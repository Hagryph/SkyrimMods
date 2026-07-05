#pragma once
#include "PCH.h"

namespace hag::console {

struct Result {
    bool        faulted = false;
    bool        noCompiler = false;
    bool        compiled = false;
    unsigned    compiledSize = 0;
    std::string output;
};

Result Run(const std::string& command);

}  // namespace hag::console
