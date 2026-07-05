#pragma once
#include <memory>
#include <spdlog/spdlog.h>

namespace hag {

// File logger -> Documents/My Games/Skyrim Special Edition/SKSE/<name>.log
class Log {
public:
    static void Init(const char* pluginName);
    static spdlog::logger* Get() { return s_logger.get(); }

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

}  // namespace hag

#define HAG_INFO(...) ::hag::Log::Get()->info(__VA_ARGS__)
#define HAG_WARN(...) ::hag::Log::Get()->warn(__VA_ARGS__)
#define HAG_ERR(...)  ::hag::Log::Get()->error(__VA_ARGS__)
