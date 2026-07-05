#include "PCH.h"
#include "Config.h"
#include "Log.h"

#include <fstream>

namespace hag {

namespace {
    // Directory this DLL lives in (Data/SKSE/Plugins under the MO2 VFS) — config sits next to it,
    // NOT in the SKSE log folder.
    std::filesystem::path DllDir() {
        HMODULE hm = nullptr;
        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DllDir), &hm);
        wchar_t buf[MAX_PATH] = {};
        const DWORD n = ::GetModuleFileNameW(hm, buf, MAX_PATH);
        return std::filesystem::path(buf, buf + n).parent_path();
    }
    std::filesystem::path ConfigPath() { return DllDir() / "HagIPC.ini"; }

    std::string Trim(std::string s) {
        const auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        const auto b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    bool ParseBool(const std::string& v, bool dflt) {
        std::string l;
        for (char c : v) l += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (l == "true" || l == "1" || l == "yes" || l == "on")  return true;
        if (l == "false" || l == "0" || l == "no" || l == "off") return false;
        return dflt;
    }
}  // namespace

Config Config::Load() {
    Config cfg;
    const auto path = ConfigPath();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        HAG_WARN("config not found at {} - using built-in defaults (Enabled=true, Port=19000).", path.string());
        return cfg;
    }

    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') continue;
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        const auto key = Trim(t.substr(0, eq));
        const auto val = Trim(t.substr(eq + 1));
        if (_stricmp(key.c_str(), "Enabled") == 0)      cfg.enabled = ParseBool(val, cfg.enabled);
        else if (_stricmp(key.c_str(), "Port") == 0)    { int p = std::atoi(val.c_str()); if (p > 0 && p < 65536) cfg.port = static_cast<std::uint16_t>(p); }
        else if (_stricmp(key.c_str(), "Token") == 0)   cfg.token = val;
    }
    HAG_INFO("config {} : enabled={} port={} token={}", path.string(), cfg.enabled, cfg.port,
             cfg.token.empty() ? "(none)" : "(set)");
    return cfg;
}

}  // namespace hag
