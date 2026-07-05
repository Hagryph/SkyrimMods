#pragma once

#include "PCH.h"
#include "SKSE_Min.h"

namespace hag::save_storage {

constexpr std::uint32_t kDefaultMaxSetEntries = 65536;

void SetSerializationInterface(skse::SerializationInterface* serialization, std::uint32_t pluginHandle);
bool Available();

bool ContainsFormIDForModule(HMODULE module, const char* setName, std::uint32_t formID);
bool AddFormIDForModule(HMODULE module, const char* setName, std::uint32_t formID, std::uint32_t maxEntries);
std::uint32_t CountFormIDsForModule(HMODULE module, const char* setName);

}  // namespace hag::save_storage
