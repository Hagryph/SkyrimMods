#include "PCH.h"
#include "SaveStorage.h"

#include "GameOffsets.h"
#include "Log.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hag::save_storage {

namespace {

constexpr std::uint32_t kUniqueID = 0x48475354;    // HGST
constexpr std::uint32_t kFormSetRecordType = 0x48534653;  // HSFS - Hag save form sets
constexpr std::uint32_t kValueRecordType = 0x48434647;    // HCFG - Hag config values
constexpr std::uint32_t kRecordVersion = 1;
constexpr std::uint32_t kMaxNameBytes = 128;
constexpr std::uint32_t kMaxValueBytes = 4096;
constexpr std::uint32_t kFormFlagDeleted = 1u << 5;

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

struct ValueKey {
    std::string owner;
    std::string config;
    std::string key;

    bool operator==(const ValueKey& rhs) const {
        return owner == rhs.owner && config == rhs.config && key == rhs.key;
    }
};

struct ValueKeyHash {
    std::size_t operator()(const ValueKey& key) const {
        std::size_t seed = std::hash<std::string>{}(key.owner);
        const auto combine = [&seed](const std::string& value) {
            const std::size_t h = std::hash<std::string>{}(value);
            seed ^= h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        };
        combine(key.config);
        combine(key.key);
        return seed;
    }
};

std::unordered_map<ValueKey, std::string, ValueKeyHash> g_values;

std::uintptr_t SkyrimBase() {
    return reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
}

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

ValueKey MakeValueKey(const std::string& owner, const std::string& configName, const std::string& key) {
    return ValueKey{SafeName(owner), SafeName(configName), SafeName(key)};
}

ValueKey MakeValueKey(HMODULE module, const char* configName, const char* key) {
    return MakeValueKey(OwnerFromModule(module), configName ? configName : "", key ? key : "");
}

bool ReadExact(skse::SerializationInterface* serialization, void* data, std::uint32_t length) {
    return serialization && serialization->ReadRecordData(data, length) == length;
}

enum class RuntimeFormState {
    Valid,
    Stale,
    Unknown,
};

RuntimeFormState RuntimeFormStateFor(std::uint32_t formID) noexcept {
    if (formID == 0) return RuntimeFormState::Stale;

    __try {
        using LookupByIDFn = void* (*)(std::uint32_t);
        auto lookup = reinterpret_cast<LookupByIDFn>(SkyrimBase() + game::form::LookupByID);
        void* form = lookup ? lookup(formID) : nullptr;
        if (!form) return RuntimeFormState::Stale;

        const auto* bytes = static_cast<const std::uint8_t*>(form);
        const auto flags = *reinterpret_cast<const std::uint32_t*>(bytes + 0x10);
        if ((flags & kFormFlagDeleted) != 0) return RuntimeFormState::Stale;
        return RuntimeFormState::Valid;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return RuntimeFormState::Unknown;
    }
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
    const auto setCount = g_sets.size();
    const auto valueCount = g_values.size();
    g_sets.clear();
    g_values.clear();
    HAG_INFO("save storage reverted; cleared {} form-ID set(s), {} config value(s)",
             setCount, valueCount);
}

void OnSave(skse::SerializationInterface* serialization) {
    if (!serialization) return;

    std::vector<std::pair<SetKey, std::vector<std::uint32_t>>> snapshot;
    std::vector<std::pair<ValueKey, std::string>> valueSnapshot;
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
        valueSnapshot.reserve(g_values.size());
        for (const auto& [key, value] : g_values) {
            valueSnapshot.emplace_back(key, value);
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

        if (!serialization->OpenRecord(kFormSetRecordType, kRecordVersion)) {
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

    std::uint32_t savedValues = 0;
    for (const auto& [key, value] : valueSnapshot) {
        const std::uint32_t ownerLen = static_cast<std::uint32_t>(key.owner.size());
        const std::uint32_t configLen = static_cast<std::uint32_t>(key.config.size());
        const std::uint32_t keyLen = static_cast<std::uint32_t>(key.key.size());
        const std::uint32_t valueLen = static_cast<std::uint32_t>(value.size());
        if (ownerLen == 0 || ownerLen > kMaxNameBytes ||
            configLen == 0 || configLen > kMaxNameBytes ||
            keyLen == 0 || keyLen > kMaxNameBytes ||
            valueLen > kMaxValueBytes) {
            HAG_WARN("save storage skipped invalid value owner='{}' config='{}' key='{}'",
                     key.owner, key.config, key.key);
            continue;
        }

        if (!serialization->OpenRecord(kValueRecordType, kRecordVersion)) {
            HAG_ERR("save storage failed to open value record owner='{}' config='{}' key='{}'",
                    key.owner, key.config, key.key);
            continue;
        }

        bool ok = true;
        ok &= serialization->WriteRecordData(&ownerLen, sizeof(ownerLen));
        ok &= serialization->WriteRecordData(&configLen, sizeof(configLen));
        ok &= serialization->WriteRecordData(&keyLen, sizeof(keyLen));
        ok &= serialization->WriteRecordData(&valueLen, sizeof(valueLen));
        ok &= serialization->WriteRecordData(key.owner.data(), ownerLen);
        ok &= serialization->WriteRecordData(key.config.data(), configLen);
        ok &= serialization->WriteRecordData(key.key.data(), keyLen);
        if (valueLen != 0) {
            ok &= serialization->WriteRecordData(value.data(), valueLen);
        }

        if (!ok) {
            HAG_ERR("save storage value write failed owner='{}' config='{}' key='{}'",
                    key.owner, key.config, key.key);
            continue;
        }
        ++savedValues;
    }

    HAG_INFO("save storage saved {} form-ID set(s), {} id(s), {} config value(s)",
             savedSets, savedIDs, savedValues);
}

bool ReadStringField(skse::SerializationInterface* serialization,
                     std::uint32_t length,
                     std::uint32_t maxLength,
                     std::string* out) {
    if (!out || length == 0 || length > maxLength) return false;
    out->assign(length, '\0');
    return ReadExact(serialization, out->data(), length);
}

void OnLoad(skse::SerializationInterface* serialization) {
    if (!serialization) return;

    {
        std::lock_guard lock(g_mutex);
        g_sets.clear();
        g_values.clear();
    }

    std::uint32_t loadedSets = 0;
    std::uint32_t loadedIDs = 0;
    std::uint32_t droppedIDs = 0;
    std::uint32_t loadedValues = 0;
    std::uint32_t type = 0;
    std::uint32_t version = 0;
    std::uint32_t length = 0;
    while (serialization->GetNextRecordInfo(&type, &version, &length)) {
        if (type != kFormSetRecordType && type != kValueRecordType) {
            HAG_WARN("save storage skipping unknown record type {:#x}", type);
            SkipBytes(serialization, length);
            continue;
        }
        if (version != kRecordVersion) {
            HAG_WARN("save storage skipping unsupported record version {}", version);
            SkipBytes(serialization, length);
            continue;
        }

        if (type == kFormSetRecordType) {
            std::uint32_t ownerLen = 0;
            std::uint32_t nameLen = 0;
            std::uint32_t count = 0;
            if (!ReadExact(serialization, &ownerLen, sizeof(ownerLen)) ||
                !ReadExact(serialization, &nameLen, sizeof(nameLen)) ||
                !ReadExact(serialization, &count, sizeof(count))) {
                HAG_ERR("save storage load failed: truncated form-set record header");
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

            std::string owner;
            std::string name;
            if (!ReadStringField(serialization, ownerLen, kMaxNameBytes, &owner) ||
                !ReadStringField(serialization, nameLen, kMaxNameBytes, &name)) {
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
                    ++droppedIDs;
                    HAG_WARN("save storage dropped unresolved form ID {:#x} for owner='{}' name='{}'",
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
            continue;
        }

        std::uint32_t ownerLen = 0;
        std::uint32_t configLen = 0;
        std::uint32_t keyLen = 0;
        std::uint32_t valueLen = 0;
        if (!ReadExact(serialization, &ownerLen, sizeof(ownerLen)) ||
            !ReadExact(serialization, &configLen, sizeof(configLen)) ||
            !ReadExact(serialization, &keyLen, sizeof(keyLen)) ||
            !ReadExact(serialization, &valueLen, sizeof(valueLen))) {
            HAG_ERR("save storage load failed: truncated config record header");
            continue;
        }

        const std::uint32_t consumedHeader =
            sizeof(ownerLen) + sizeof(configLen) + sizeof(keyLen) + sizeof(valueLen);
        if (ownerLen == 0 || ownerLen > kMaxNameBytes ||
            configLen == 0 || configLen > kMaxNameBytes ||
            keyLen == 0 || keyLen > kMaxNameBytes ||
            valueLen > kMaxValueBytes) {
            HAG_ERR("save storage load rejected malformed config ownerLen={} configLen={} keyLen={} valueLen={}",
                    ownerLen, configLen, keyLen, valueLen);
            SkipBytes(serialization, length > consumedHeader ? length - consumedHeader : 0);
            continue;
        }

        std::string owner;
        std::string configName;
        std::string keyName;
        std::string value(valueLen, '\0');
        if (!ReadStringField(serialization, ownerLen, kMaxNameBytes, &owner) ||
            !ReadStringField(serialization, configLen, kMaxNameBytes, &configName) ||
            !ReadStringField(serialization, keyLen, kMaxNameBytes, &keyName)) {
            HAG_ERR("save storage load failed: truncated config names");
            continue;
        }
        if (valueLen != 0 && !ReadExact(serialization, value.data(), valueLen)) {
            HAG_ERR("save storage load failed: truncated config value owner='{}' config='{}' key='{}'",
                    owner, configName, keyName);
            continue;
        }

        ValueKey key = MakeValueKey(owner, configName, keyName);
        {
            std::lock_guard lock(g_mutex);
            g_values[std::move(key)] = std::move(value);
        }
        ++loadedValues;
    }

    HAG_INFO("save storage loaded {} form-ID set(s), {} id(s), dropped {} unresolved id(s), {} config value(s)",
             loadedSets, loadedIDs, droppedIDs, loadedValues);
}

void OnFormDelete(std::uint64_t handle) {
    const auto formID = static_cast<std::uint32_t>(handle & 0xffffffffu);
    if (formID == 0) return;

    std::uint32_t touchedSets = 0;
    {
        std::lock_guard lock(g_mutex);
        for (auto& [key, values] : g_sets) {
            if (values.erase(formID) != 0) {
                ++touchedSets;
                HAG_INFO("save storage pruned deleted form {:#x} owner='{}' name='{}' count={}",
                         formID, key.owner, key.name, values.size());
            }
        }
    }

    if (touchedSets != 0) {
        HAG_INFO("save storage form-delete callback pruned form {:#x} from {} set(s)",
                 formID, touchedSets);
    }
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
    g_serialization->SetFormDeleteCallback(g_pluginHandle, reinterpret_cast<void*>(&OnFormDelete));
    g_available = true;
    HAG_INFO("save storage registered SKSE co-save callbacks");
}

bool Available() {
    return g_available;
}

std::uint32_t PruneRuntimeFormIDSets(const char* reason) {
    if (!g_available) return 0;

    std::uint32_t removed = 0;
    std::uint32_t kept = 0;
    std::uint32_t unknown = 0;
    {
        std::lock_guard lock(g_mutex);
        for (auto& [key, values] : g_sets) {
            for (auto it = values.begin(); it != values.end();) {
                const std::uint32_t formID = *it;
                const RuntimeFormState state = RuntimeFormStateFor(formID);
                if (state == RuntimeFormState::Stale) {
                    HAG_INFO("save storage runtime-pruned stale form {:#x} owner='{}' name='{}' ({})",
                             formID, key.owner, key.name, reason ? reason : "unspecified");
                    it = values.erase(it);
                    ++removed;
                    continue;
                }
                if (state == RuntimeFormState::Unknown) {
                    ++unknown;
                } else {
                    ++kept;
                }
                ++it;
            }
        }
    }

    if (removed != 0 || unknown != 0) {
        HAG_INFO("save storage runtime prune complete: removed={} kept={} unknown={} ({})",
                 removed, kept, unknown, reason ? reason : "unspecified");
    }
    return removed;
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

std::string GetValueForOwner(const std::string& owner,
                             const std::string& configName,
                             const std::string& keyName,
                             const std::string& defaultValue) {
    if (!g_available || owner.empty() || configName.empty() || keyName.empty()) {
        return defaultValue;
    }

    const ValueKey key = MakeValueKey(owner, configName, keyName);
    std::lock_guard lock(g_mutex);
    const auto it = g_values.find(key);
    if (it != g_values.end()) {
        return it->second;
    }

    g_values.emplace(key, defaultValue);
    HAG_INFO("save storage config default staged owner='{}' config='{}' key='{}' value='{}'",
             key.owner, key.config, key.key, defaultValue);
    return defaultValue;
}

bool SetValueForOwner(const std::string& owner,
                      const std::string& configName,
                      const std::string& keyName,
                      const std::string& value) {
    if (!g_available || owner.empty() || configName.empty() || keyName.empty() ||
        value.size() > kMaxValueBytes) {
        return false;
    }

    const ValueKey key = MakeValueKey(owner, configName, keyName);
    std::lock_guard lock(g_mutex);
    g_values[key] = value;
    HAG_INFO("save storage config value set owner='{}' config='{}' key='{}' value='{}'",
             key.owner, key.config, key.key, value);
    return true;
}

std::string GetValueForModule(HMODULE module,
                              const char* configName,
                              const char* keyName,
                              const char* defaultValue) {
    if (!module || !configName || !*configName || !keyName || !*keyName) {
        return defaultValue ? defaultValue : "";
    }
    return GetValueForOwner(
        OwnerFromModule(module), configName, keyName, defaultValue ? defaultValue : "");
}

bool SetValueForModule(HMODULE module,
                       const char* configName,
                       const char* keyName,
                       const char* value) {
    if (!module || !configName || !*configName || !keyName || !*keyName || !value) {
        return false;
    }
    return SetValueForOwner(OwnerFromModule(module), configName, keyName, value);
}

}  // namespace hag::save_storage
