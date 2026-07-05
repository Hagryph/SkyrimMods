#include "PCH.h"
#include "ipc/SaveLoad.h"
#include "Offsets.h"

#include <windows.h>

// The game calls (save-dir getter, load) touch game state, so they live in POD-only __try blocks
// (no C++ unwinding objects inside), same discipline as CallExec.cpp / ConsoleExec.cpp.
namespace hag::saveload {

namespace {

using GetSng_t    = void* (*)();
using GetDir_t    = void  (*)(void*, char*, int);
using Load_t      = void  (*)(void*, const char*, unsigned, unsigned, int);
using BeginLoad_t = void  (*)();

// Fill 'out' with the game's resolved save directory (VFS-aware; under MO2 this is ...\__MO_Saves\).
// Returns false on fault or if unavailable.
bool GameSaveDir(char* out, int cap) noexcept {
    __try {
        out[0] = '\0';
        void* sng = reinterpret_cast<GetSng_t>(offsets::FromRVA(offsets::kSaveDirSng))();
        if (!sng) return false;
        void** vt   = *reinterpret_cast<void***>(sng);
        void*  slot = *reinterpret_cast<void**>(reinterpret_cast<char*>(vt) + offsets::kSaveDirSlot);
        reinterpret_cast<GetDir_t>(slot)(sng, out, 0);
        out[cap - 1] = '\0';
        return out[0] != '\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        return false;
    }
}

// Reproduces the console `load` handler FUN_14036ebb0: flag, begin-load, enqueue the load request.
bool LoadGuarded(const char* name) noexcept {
    __try {
        void* mgr = *reinterpret_cast<void**>(offsets::FromRVA(offsets::kManagerPtr));
        if (!mgr) return false;
        *reinterpret_cast<unsigned char*>(offsets::FromRVA(offsets::kLoadFlag)) = 1;
        reinterpret_cast<BeginLoad_t>(offsets::FromRVA(offsets::kBeginLoad))();
        // 0xffffffff / flags=0 / 1 : exactly what the console `load <name>` handler passes.
        reinterpret_cast<Load_t>(offsets::FromRVA(offsets::kLoad))(mgr, name, 0xffffffffu, 0u, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::vector<std::string> ListSaves() {
    std::vector<std::string> out;

    // Use the game's OWN save directory, not a hard-coded path (MO2 profile saves live under
    // ...\__MO_Saves\, which the game resolves and its VFS maps to the active profile).
    char dir[0x108];
    if (!GameSaveDir(dir, sizeof(dir)) || !dir[0]) return out;

    std::string pattern(dir);
    if (pattern.back() != '\\' && pattern.back() != '/') pattern += '\\';
    pattern += "*.ess";

    WIN32_FIND_DATAA fd{};
    HANDLE h = ::FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = fd.cFileName;
        if (name.size() > 4) name.resize(name.size() - 4);  // strip ".ess"
        out.push_back(std::move(name));
    } while (::FindNextFileA(h, &fd));
    ::FindClose(h);
    return out;
}

bool Load(const std::string& saveName) {
    if (saveName.empty()) return false;
    return LoadGuarded(saveName.c_str());
}

}  // namespace hag::saveload
