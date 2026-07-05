#include "PCH.h"
#include "Config.h"
#include "Log.h"

#include <fstream>

namespace hag {

namespace {
    // Directory this DLL lives in (Data\SKSE\Plugins under the MO2 VFS) — config sits next
    // to it, matching HagIPC. Resolved from an address inside this module.
    std::filesystem::path DllDir() {
        HMODULE hm = nullptr;
        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DllDir), &hm);
        wchar_t buf[MAX_PATH] = {};
        const DWORD n = ::GetModuleFileNameW(hm, buf, MAX_PATH);
        return std::filesystem::path(buf, buf + n).parent_path();
    }
    std::filesystem::path ConfigPath() { return DllDir() / "HagGeneral.ini"; }

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

Config& Config::Get() {
    static Config s;
    return s;
}

void Config::Load() {
    const auto path = ConfigPath();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        HAG_WARN("config not found at {} - writing defaults (Fullscreen={}, Borderless={}, AlwaysActive={}).",
                 path.string(), fullscreen, borderless, alwaysActive);
        Save();
        return;
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
        if (_stricmp(key.c_str(), "Fullscreen") == 0)        fullscreen   = ParseBool(val, fullscreen);
        else if (_stricmp(key.c_str(), "Borderless") == 0)   borderless   = ParseBool(val, borderless);
        else if (_stricmp(key.c_str(), "AlwaysActive") == 0) alwaysActive = ParseBool(val, alwaysActive);
    }
    HAG_INFO("config {} : Fullscreen={} Borderless={} AlwaysActive={}",
             path.string(), fullscreen, borderless, alwaysActive);
}

void Config::Save() const {
    const auto path = ConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::trunc);
    if (!out) { HAG_ERR("failed to write config {}", path.string()); return; }
    out << "; HagGeneral - global settings (auto-saved by the in-game HagUI page).\n"
        << "; Fullscreen/Borderless are imposed at the D3D swapchain hook -> effect on next game launch.\n"
        << "[General]\n"
        << "Fullscreen="   << (fullscreen   ? "true" : "false") << "\n"
        << "Borderless="   << (borderless   ? "true" : "false") << "\n"
        << "AlwaysActive=" << (alwaysActive ? "true" : "false") << "\n";
    HAG_INFO("config saved -> {}", path.string());
}

}  // namespace hag
