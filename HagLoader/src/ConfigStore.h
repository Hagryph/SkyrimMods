#pragma once

#include "PCH.h"

namespace hag::config_store {

enum class Scope { Global, PerSave };

bool GetBool(Scope scope, const std::string& modName, const std::string& key, bool defaultValue);
bool SetBool(Scope scope, const std::string& modName, const std::string& key, bool value);
std::int64_t GetInt(Scope scope, const std::string& modName, const std::string& key, std::int64_t defaultValue);
bool SetInt(Scope scope, const std::string& modName, const std::string& key, std::int64_t value);
bool GetBoolForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, bool defaultValue);
bool SetBoolForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, bool value);
std::int64_t GetIntForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, std::int64_t defaultValue);
bool SetIntForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, std::int64_t value);

}  // namespace hag::config_store
