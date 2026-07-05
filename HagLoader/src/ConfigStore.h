#pragma once

#include "PCH.h"

namespace hag::config_store {

enum class Scope { Global, PerSave };

bool GetBool(Scope scope, const std::string& modName, const std::string& key, bool defaultValue);
bool SetBool(Scope scope, const std::string& modName, const std::string& key, bool value);
bool GetBoolForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, bool defaultValue);
bool SetBoolForModule(HMODULE module, Scope scope, const std::string& configName, const std::string& key, bool value);

}  // namespace hag::config_store
