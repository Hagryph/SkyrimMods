#include "PCH.h"
#include "ModManager.h"

#include "Log.h"
#include "SkyrimModAPI.h"
#include "api/HagApi.h"

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace hag {

ModManager& ModManager::Get() {
    static ModManager m;
    return m;
}

namespace {

std::wstring PluginDir() {
    wchar_t path[MAX_PATH]{};
    HMODULE self = ::GetModuleHandleW(L"HagUI.dll");
    ::GetModuleFileNameW(self, path, MAX_PATH);

    std::wstring p = path;
    const size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring(L".") : p.substr(0, slash);
}

std::wstring SelfPath() {
    wchar_t path[MAX_PATH]{};
    HMODULE self = ::GetModuleHandleW(L"HagUI.dll");
    ::GetModuleFileNameW(self, path, MAX_PATH);
    return path;
}

std::string Narrow(const std::wstring& w) {
    char b[MAX_PATH]{};
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, b, MAX_PATH, nullptr, nullptr);
    return b;
}

}  // namespace

void ModManager::LoadAll() {
    if (loaded_) return;
    loaded_ = true;

    const std::wstring dir = PluginDir();
    const std::wstring selfPath = SelfPath();

    std::vector<std::wstring> dlls;
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW((dir + L"\\*.dll").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                dlls.emplace_back(fd.cFileName);
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    std::sort(dlls.begin(), dlls.end());

    const std::string ndir = Narrow(dir);
    HAG_INFO("scanning external HagUI mods in {}", ndir);

    int idx = 0;
    for (const auto& name : dlls) {
        const std::wstring full = dir + L"\\" + name;
        if (_wcsicmp(full.c_str(), selfPath.c_str()) == 0) {
            continue;
        }

        const std::string file = Narrow(name);
        HMODULE mod = ::LoadLibraryW(full.c_str());
        if (!mod) {
            HAG_ERR("mod {} failed to load (Win32 error {})", file, ::GetLastError());
            continue;
        }

        auto init = reinterpret_cast<SkyrimMod_Init_t>(::GetProcAddress(mod, "SkyrimMod_Init"));
        auto nameFn = reinterpret_cast<SkyrimMod_Name_t>(::GetProcAddress(mod, "SkyrimMod_Name"));
        auto scopeFn = reinterpret_cast<SkyrimMod_Scope_t>(::GetProcAddress(mod, "SkyrimMod_Scope"));
        auto onDataLoaded = reinterpret_cast<SkyrimMod_OnDataLoaded_t>(::GetProcAddress(mod, "SkyrimMod_OnDataLoaded"));
        if (!init && !nameFn && !scopeFn && !onDataLoaded) {
            ::FreeLibrary(mod);
            continue;
        }

        ++idx;

        std::string tabName = file;
        if (nameFn) {
            if (const char* n = nameFn(); n && *n) tabName = n;
        }
        if (size_t dot = tabName.rfind(".dll"); dot != std::string::npos) {
            tabName.erase(dot);
        }

        int scope = SKYRIMMOD_GLOBAL;
        if (scopeFn) {
            scope = scopeFn();
        }

        auto& page = api::HagUI::Get().RegisterPage(
            tabName,
            scope == SKYRIMMOD_PERSAVE ? api::Scope::PerSave : api::Scope::Global);

        HAG_INFO("mod {}. {} loaded @ {} tab='{}' [{}]{}",
                 idx, file, static_cast<void*>(mod), tabName,
                 scope == SKYRIMMOD_PERSAVE ? "save-local" : "global",
                 init ? "" : " (no SkyrimMod_Init)");
        if (init) {
            init(reinterpret_cast<void*>(&page));
        }
        mods_.push_back(LoadedMod{ mod, onDataLoaded });
    }

    if (idx == 0) {
        HAG_WARN("no external HagUI contract mods found in {}", ndir);
    }
}

void ModManager::OnDataLoaded() {
    for (const LoadedMod& mod : mods_) {
        if (mod.onDataLoaded) {
            mod.onDataLoaded();
        }
    }
}

}  // namespace hag
