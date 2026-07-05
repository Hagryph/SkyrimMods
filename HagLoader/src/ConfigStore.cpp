#include "PCH.h"
#include "ConfigStore.h"
#include "Log.h"

#include <cctype>
#include <fstream>
#include <map>

namespace hag::config_store {

namespace {

std::string SafeName(std::string value) {
    for (char& c : value) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) c = '_';
    }
    return value.empty() ? "Unknown" : value;
}

std::filesystem::path ModuleRootFromHandle(HMODULE hm) {
    if (!hm) return {};

    wchar_t buf[MAX_PATH] = {};
    const DWORD n = ::GetModuleFileNameW(hm, buf, MAX_PATH);
    if (n == 0) return {};
    auto plugins = std::filesystem::path(buf, buf + n).parent_path();
    return plugins.parent_path().parent_path();
}

std::filesystem::path ModuleRootFromName(const std::string& safeModName) {
    const std::string dllName = safeModName + ".dll";
    std::wstring wideName(dllName.begin(), dllName.end());
    HMODULE hm = ::GetModuleHandleW(wideName.c_str());
    if (!hm) {
        HAG_WARN("config store could not resolve module {}; falling back to HagLoader.dll", dllName);
        hm = ::GetModuleHandleW(L"HagLoader.dll");
    }
    return ModuleRootFromHandle(hm);
}

std::filesystem::path ConfigPathFromRoot(const std::filesystem::path& root, Scope scope, const std::string& configName) {
    const std::string safeConfig = SafeName(configName);
    if (root.empty()) return {};
    if (scope == Scope::Global) {
        return root / (safeConfig + ".ini");
    }
    // TODO: replace Current with a real save identifier once save-name RE is added.
    return root / "PerSave" / (safeConfig + ".Current.ini");
}

std::filesystem::path ConfigPath(Scope scope, const std::string& modName) {
    const std::string safeMod = SafeName(modName);
    return ConfigPathFromRoot(ModuleRootFromName(safeMod), scope, safeMod);
}

std::string Trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool ParseBool(const std::string& v, bool dflt) {
    std::string l;
    for (char c : v) l += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (l == "true" || l == "1" || l == "yes" || l == "on") return true;
    if (l == "false" || l == "0" || l == "no" || l == "off") return false;
    return dflt;
}

std::int64_t ParseInt(const std::string& v, std::int64_t dflt) {
    try {
        std::size_t consumed = 0;
        const auto out = std::stoll(v, &consumed, 10);
        return consumed == v.size() ? out : dflt;
    } catch (...) {
        return dflt;
    }
}

std::map<std::string, std::string> LoadFile(const std::filesystem::path& path) {
    std::map<std::string, std::string> values;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') continue;
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        values[Trim(t.substr(0, eq))] = Trim(t.substr(eq + 1));
    }
    return values;
}

bool SaveFile(const std::filesystem::path& path, const std::map<std::string, std::string>& values) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "; HagLoader managed config for this mod. Mods should read/write this through HagLoaderAPI.\n"
        << "[Values]\n";
    for (const auto& [key, value] : values) {
        out << key << "=" << value << "\n";
    }
    return true;
}

}  // namespace

bool GetBool(Scope scope, const std::string& modName, const std::string& key, bool defaultValue) {
    if (modName.empty() || key.empty()) return defaultValue;
    const auto path = ConfigPath(scope, modName);
    if (path.empty()) return defaultValue;
    auto values = LoadFile(path);
    const auto it = values.find(key);
    if (it != values.end()) {
        return ParseBool(it->second, defaultValue);
    }

    values[key] = defaultValue ? "true" : "false";
    if (SaveFile(path, values)) {
        HAG_INFO("config default saved: {} {}={}", path.string(), key, defaultValue);
    } else {
        HAG_ERR("config default save failed: {}", path.string());
    }
    return defaultValue;
}

bool SetBool(Scope scope, const std::string& modName, const std::string& key, bool value) {
    if (modName.empty() || key.empty()) return false;
    const auto path = ConfigPath(scope, modName);
    if (path.empty()) return false;
    auto values = LoadFile(path);
    values[key] = value ? "true" : "false";
    if (!SaveFile(path, values)) {
        HAG_ERR("config save failed: {}", path.string());
        return false;
    }
    HAG_INFO("config saved: {} {}={}", path.string(), key, value);
    return true;
}

std::int64_t GetInt(Scope scope, const std::string& modName, const std::string& key, std::int64_t defaultValue) {
    if (modName.empty() || key.empty()) return defaultValue;
    const auto path = ConfigPath(scope, modName);
    if (path.empty()) return defaultValue;
    auto values = LoadFile(path);
    const auto it = values.find(key);
    if (it != values.end()) {
        return ParseInt(it->second, defaultValue);
    }

    values[key] = std::to_string(defaultValue);
    if (SaveFile(path, values)) {
        HAG_INFO("config default saved: {} {}={}", path.string(), key, defaultValue);
    } else {
        HAG_ERR("config default save failed: {}", path.string());
    }
    return defaultValue;
}

bool SetInt(Scope scope, const std::string& modName, const std::string& key, std::int64_t value) {
    if (modName.empty() || key.empty()) return false;
    const auto path = ConfigPath(scope, modName);
    if (path.empty()) return false;
    auto values = LoadFile(path);
    values[key] = std::to_string(value);
    if (!SaveFile(path, values)) {
        HAG_ERR("config save failed: {}", path.string());
        return false;
    }
    HAG_INFO("config saved: {} {}={}", path.string(), key, value);
    return true;
}

bool GetBoolForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, bool defaultValue) {
    if (!module || configName.empty() || key.empty()) return defaultValue;
    const auto path = ConfigPathFromRoot(ModuleRootFromHandle(module), scope, configName);
    if (path.empty()) return defaultValue;
    auto values = LoadFile(path);
    const auto it = values.find(key);
    if (it != values.end()) {
        return ParseBool(it->second, defaultValue);
    }

    values[key] = defaultValue ? "true" : "false";
    if (SaveFile(path, values)) {
        HAG_INFO("config default saved: {} {}={}", path.string(), key, defaultValue);
    } else {
        HAG_ERR("config default save failed: {}", path.string());
    }
    return defaultValue;
}

bool SetBoolForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, bool value) {
    if (!module || configName.empty() || key.empty()) return false;
    const auto path = ConfigPathFromRoot(ModuleRootFromHandle(module), scope, configName);
    if (path.empty()) return false;
    auto values = LoadFile(path);
    values[key] = value ? "true" : "false";
    if (!SaveFile(path, values)) {
        HAG_ERR("config save failed: {}", path.string());
        return false;
    }
    HAG_INFO("config saved: {} {}={}", path.string(), key, value);
    return true;
}

std::int64_t GetIntForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, std::int64_t defaultValue) {
    if (!module || configName.empty() || key.empty()) return defaultValue;
    const auto path = ConfigPathFromRoot(ModuleRootFromHandle(module), scope, configName);
    if (path.empty()) return defaultValue;
    auto values = LoadFile(path);
    const auto it = values.find(key);
    if (it != values.end()) {
        return ParseInt(it->second, defaultValue);
    }

    values[key] = std::to_string(defaultValue);
    if (SaveFile(path, values)) {
        HAG_INFO("config default saved: {} {}={}", path.string(), key, defaultValue);
    } else {
        HAG_ERR("config default save failed: {}", path.string());
    }
    return defaultValue;
}

bool SetIntForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, std::int64_t value) {
    if (!module || configName.empty() || key.empty()) return false;
    const auto path = ConfigPathFromRoot(ModuleRootFromHandle(module), scope, configName);
    if (path.empty()) return false;
    auto values = LoadFile(path);
    values[key] = std::to_string(value);
    if (!SaveFile(path, values)) {
        HAG_ERR("config save failed: {}", path.string());
        return false;
    }
    HAG_INFO("config saved: {} {}={}", path.string(), key, value);
    return true;
}

}  // namespace hag::config_store
