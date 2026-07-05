#include "PCH.h"
#include "Log.h"

#include <ShlObj.h>
#include <spdlog/sinks/basic_file_sink.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace hag {

std::shared_ptr<spdlog::logger> Log::s_logger;

void Log::Init(const char* pluginName) {
    std::filesystem::path dir;
    PWSTR docs = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        dir = docs;
        ::CoTaskMemFree(docs);
    }
    dir /= L"My Games";
    dir /= L"Skyrim Special Edition";
    dir /= L"SKSE";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const auto file = (dir / (std::string(pluginName) + ".log")).string();
    s_logger = spdlog::basic_logger_mt(pluginName, file, true);
    s_logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
    s_logger->set_level(spdlog::level::trace);
    s_logger->flush_on(spdlog::level::trace);
}

}  // namespace hag
