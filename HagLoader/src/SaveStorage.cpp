#include "PCH.h"
#include "SaveStorage.h"

#include "Log.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hag::save_storage {

namespace {

constexpr std::uint32_t kUniqueID = 0x48475354;    // HGST
constexpr std::uint32_t kRecordType = 0x48534653;  // HSFS - Hag save form sets
constexpr std::uint32_t kRecordVersion = 1;
constexpr std::uint32_t kMaxNameBytes = 128;

skse::SerializationInterface* g_serialization = nullptr;
std::uint32_t g_pluginHandle = 0;
bool g_available = false;
std::mutex g_mutex;

struct SetKey {
    std::string owner;
    std::string name;

    bool operator==(const SetKey& rhs) const {
        return owner == rhs.owner && name == rhs.name;
    }
};

struct SetKeyHash {
    std::size_t operator()(const SetKey& key) const {
        const std::size_t a = std::hash<std::string>{}(key.owner);
        const std::size_t b = std::hash<std::string>{}(key.name);
        return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
    }
};

using FormSet = std::unordered_set<std::uint32_t>;
std::unordered_map<SetKey, FormSet, SetKeyHash> g_sets;

std::string SafeName(std::string value) {
    for (char& c : value) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) c = '_';
    }
    return value.empty() ? "Unknown" : value;
}

std::string OwnerFromModule(HMODULE module) {
    if (!module) return "Unknown";

    wchar_t buf[MAX_PATH] = {};
    const DWORD n = ::GetModuleFileNameW(module, buf, MAX_PATH);
    if (n == 0) return "Unknown";

    std::filesystem::path path(buf, buf + n);
    std::wstring wide = path.stem().wstring();
    std::string narrow;
    narrow.resize(wide.size());
    std::transform(wide.begin(), wide.end(), narrow.begin(), [](wchar_t c) {
        return c >= 0 && c <= 0x7f ? static_cast<char>(c) : '_';
    });
    return SafeName(std::move(narrow));
}

SetKey MakeKey(HMODULE module, const char* setName) {
    return SetKey{OwnerFromModule(module), SafeName(setName ? setName : "")};
}

bool ReadExact(skse::SerializationInterface* serialization, void* data, std::uint32_t length) {
    return serialization && serialization->ReadRecordData(data, length) == length;
}

void SkipBytes(skse::SerializationInterface* serialization, std::uint32_t length) {
    if (!serialization) return;
    char scratch[256] = {};
    while (length > 0) {
        const std::uint32_t chunk = std::min<std::uint32_t>(length, sizeof(scratch));
        const std::uint32_t read = serialization->ReadRecordData(scratch, chunk);
        if (read == 0) return;
        length -= read;
    }
}

void OnRevert(skse::SerializationInterface*) {
    std::lock_guard lock(g_mutex);
    const auto count = g_sets.size();
    g_sets.clear();
    HAG_INFO("save storage reverted; cleared {} form-ID set(s)", count);
}

void OnSave(skse::SerializationInterface* serialization) {
    if (!serialization) return;

    std::vector<std::pair<SetKey, std::vector<std::uint32_t>>> snapshot;
    {
        std::lock_guard lock(g_mutex);
        snapshot.reserve(g_sets.size());
        for (const auto& [key, values] : g_sets) {
            if (values.empty()) continue;
            auto& item = snapshot.emplace_back();
            item.first = key;
            item.second.assign(values.begin(), values.end());
            std::sort(item.second.begin(), item.second.end());
        }
    }

    std::uint32_t savedSets = 0;
    std::uint32_t savedIDs = 0;
    for (const auto& [key, ids] : snapshot) {
        const std::uint32_t ownerLen = static_cast<std::uint32_t>(key.owner.size());
        const std::uint32_t nameLen = static_cast<std::uint32_t>(key.name.size());
        const std::uint32_t count = static_cast<std::uint32_t>(ids.size());
        if (ownerLen == 0 || ownerLen > kMaxNameBytes || nameLen == 0 || nameLen > kMaxNameBytes) {
            HAG_WARN("save storage skipped invalid set owner='{}' name='{}'", key.owner, key.name);
            continue;
        }

        if (!serialization->OpenRecord(kRecordType, kRecordVersion)) {
            HAG_ERR("save storage failed to open record owner='{}' name='{}'", key.owner, key.name);
            continue;
        }

        bool ok = true;
        ok &= serialization->WriteRecordData(&ownerLen, sizeof(ownerLen));
        ok &= serialization->WriteRecordData(&nameLen, sizeof(nameLen));
        ok &= serialization->WriteRecordData(&count, sizeof(count));
        ok &= serialization->WriteRecordData(key.owner.data(), ownerLen);
        ok &= serialization->WriteRecordData(key.name.data(), nameLen);
        if (count != 0) {
            ok &= serialization->WriteRecordData(ids.data(), count * sizeof(std::uint32_t));
        }

        if (!ok) {
            HAG_ERR("save storage write failed owner='{}' name='{}'", key.owner, key.name);
            continue;
        }
        ++savedSets;
        savedIDs += count;
    }

    HAG_INFO("save storage saved {} form-ID set(s), {} id(s)", savedSets, savedIDs);
}

void OnLoad(skse::SerializationInterface* serialization) {
    if (!serialization) return;

    {
        std::lock_guard lock(g_mutex);
        g_sets.clear();
    }

    std::uint32_t loadedSets = 0;
    std::uint32_t loadedIDs = 0;
    std::uint32_t type = 0;
    std::uint32_t version = 0;
    std::uint32_t length = 0;
    while (serialization->GetNextRecordInfo(&type, &version, &length)) {
        if (type != kRecordType) {
            HAG_WARN("save storage skipping unknown record type {:#x}", type);
            SkipBytes(serialization, length);
            continue;
        }
        if (version != kRecordVersion) {
            HAG_WARN("save storage skipping unsupported record version {}", version);
            SkipBytes(serialization, length);
            continue;
        }

        std::uint32_t ownerLen = 0;
        std::uint32_t nameLen = 0;
        std::uint32_t count = 0;
        if (!ReadExact(serialization, &ownerLen, sizeof(ownerLen)) ||
            !ReadExact(serialization, &nameLen, sizeof(nameLen)) ||
            !ReadExact(serialization, &count, sizeof(count))) {
            HAG_ERR("save storage load failed: truncated record header");
            continue;
        }

        const std::uint32_t consumedHeader = sizeof(ownerLen) + sizeof(nameLen) + sizeof(count);
        if (ownerLen == 0 || ownerLen > kMaxNameBytes || nameLen == 0 || nameLen > kMaxNameBytes ||
            count > kDefaultMaxSetEntries) {
            HAG_ERR("save storage load rejected malformed set ownerLen={} nameLen={} count={}",
                    ownerLen, nameLen, count);
            SkipBytes(serialization, length > consumedHeader ? length - consumedHeader : 0);
            continue;
        }

        std::string owner(ownerLen, '\0');
        std::string name(nameLen, '\0');
        if (!ReadExact(serialization, owner.data(), ownerLen) ||
            !ReadExact(serialization, name.data(), nameLen)) {
            HAG_ERR("save storage load failed: truncated set names");
            continue;
        }

        SetKey key{SafeName(std::move(owner)), SafeName(std::move(name))};
        FormSet values;
        values.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t oldID = 0;
            if (!ReadExact(serialization, &oldID, sizeof(oldID))) {
                HAG_ERR("save storage load failed: truncated form-ID list owner='{}' name='{}'",
                        key.owner, key.name);
                break;
            }
            if (oldID == 0) continue;

            std::uint32_t resolvedID = 0;
            if (serialization->ResolveFormId && serialization->ResolveFormId(oldID, &resolvedID)) {
                if (resolvedID != 0) values.insert(resolvedID);
            } else {
                HAG_WARN("save storage could not resolve form ID {:#x} for owner='{}' name='{}'",
                         oldID, key.owner, key.name);
            }
        }

        const std::uint32_t setCount = static_cast<std::uint32_t>(values.size());
        {
            std::lock_guard lock(g_mutex);
            auto& dst = g_sets[key];
            dst.insert(values.begin(), values.end());
        }
        ++loadedSets;
        loadedIDs += setCount;
    }

    HAG_INFO("save storage loaded {} form-ID set(s), {} id(s)", loadedSets, loadedIDs);
}

}  // namespace

void SetSerializationInterface(skse::SerializationInterface* serialization, std::uint32_t pluginHandle) {
    g_serialization = serialization;
    g_pluginHandle = pluginHandle;
    g_available = false;

    if (!g_serialization) {
        HAG_ERR("save storage unavailable: SKSE serialization interface is null");
        return;
    }

    g_serialization->SetUniqueID(g_pluginHandle, kUniqueID);
    g_serialization->SetSaveCallback(g_pluginHandle, reinterpret_cast<void*>(&OnSave));
    g_serialization->SetLoadCallback(g_pluginHandle, reinterpret_cast<void*>(&OnLoad));
    g_serialization->SetRevertCallback(g_pluginHandle, reinterpret_cast<void*>(&OnRevert));
    g_available = true;
    HAG_INFO("save storage registered SKSE co-save callbacks");
}

bool Available() {
    return g_available;
}

bool ContainsFormIDForModule(HMODULE module, const char* setName, std::uint32_t formID) {
    if (!module || !setName || !*setName || formID == 0) return false;
    const SetKey key = MakeKey(module, setName);
    std::lock_guard lock(g_mutex);
    const auto it = g_sets.find(key);
    return it != g_sets.end() && it->second.contains(formID);
}

bool AddFormIDForModule(HMODULE module, const char* setName, std::uint32_t formID, std::uint32_t maxEntries) {
    if (!g_available || !module || !setName || !*setName || formID == 0) return false;
    const std::uint32_t cap = maxEntries == 0 ? kDefaultMaxSetEntries : maxEntries;
    const SetKey key = MakeKey(module, setName);
    std::lock_guard lock(g_mutex);
    auto& values = g_sets[key];
    if (values.contains(formID)) return true;
    if (values.size() >= cap) {
        HAG_ERR("save storage set full owner='{}' name='{}' cap={}", key.owner, key.name, cap);
        return false;
    }
    values.insert(formID);
    HAG_INFO("save storage recorded form {:#x} owner='{}' name='{}' count={}",
             formID, key.owner, key.name, values.size());
    return true;
}

std::uint32_t CountFormIDsForModule(HMODULE module, const char* setName) {
    if (!module || !setName || !*setName) return 0;
    const SetKey key = MakeKey(module, setName);
    std::lock_guard lock(g_mutex);
    const auto it = g_sets.find(key);
    return it == g_sets.end() ? 0 : static_cast<std::uint32_t>(it->second.size());
}

}  // namespace hag::save_storage
