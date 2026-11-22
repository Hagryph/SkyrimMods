#include "PCH.h"
#include "ipc/Settings.h"
#include "ipc/MainThread.h"
#include "HagUIAPI.h"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

namespace hag::ipc {
namespace {

constexpr const char* kUsage = "err usage: settings list [page] | settings get <page> <id> | settings set <page> <id> <value>";

const char* Error(int32_t status) {
    switch (status) {
    case HAGUI_SETTING_NOT_FOUND: return "err setting not found (use settings list)";
    case HAGUI_SETTING_NO_SAVE: return "err setting requires a loaded save";
    case HAGUI_SETTING_DISABLED: return "err setting is disabled";
    case HAGUI_SETTING_INVALID: return "err invalid setting value";
    default: return "err setting request failed";
    }
}

std::string QuoteJson(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (c < 0x20) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    out += '"';
    return out;
}

std::string ValueJson(HagUI_Value value) {
    switch (value.type) {
    case HAGUI_VT_BOOL: return value.b ? "true" : "false";
    case HAGUI_VT_INT: return std::to_string(value.i);
    case HAGUI_VT_DOUBLE: {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.d);
        return std::isfinite(value.d) && result.ec == std::errc{} ? std::string(buffer, result.ptr) : "null";
    }
    case HAGUI_VT_STRING: return QuoteJson(value.s ? value.s : "");
    default: return "null";
    }
}

const char* TypeName(int32_t inputType) {
    switch (inputType) {
    case HAGUI_VT_BOOL: return "bool";
    case HAGUI_VT_INT: return "keycode";
    case HAGUI_VT_DOUBLE: return "number";
    default: return "string";
    }
}

bool ParseInput(const std::string& value, int32_t type, HagUI_Value& out) {
    const auto* first = value.data();
    const auto* last = first + value.size();
    out.type = type;
    switch (type) {
    case HAGUI_VT_BOOL:
        if (value == "true" || value == "on" || value == "1") out.b = true;
        else if (value == "false" || value == "off" || value == "0") out.b = false;
        else return false;
        return true;
    case HAGUI_VT_INT: {
        const bool hex = value.starts_with("0x") || value.starts_with("0X");
        const auto r = std::from_chars(first + (hex ? 2 : 0), last, out.i, hex ? 16 : 10);
        return r.ec == std::errc{} && r.ptr == last;
    }
    case HAGUI_VT_DOUBLE: {
        const auto r = std::from_chars(first, last, out.d);
        return r.ec == std::errc{} && r.ptr == last && std::isfinite(out.d);
    }
    case HAGUI_VT_STRING: out.s = value.c_str(); return true;
    default: return false;
    }
}

void AppendSetting(void* user, const HagUI_Setting* setting) {
    auto& json = *static_cast<std::string*>(user);
    if (!json.empty()) json += ',';
    json += "{\"page\":" + QuoteJson(setting->page) + ",\"id\":" + QuoteJson(setting->id) +
            ",\"label\":" + QuoteJson(setting->label) + ",\"scope\":" +
            QuoteJson(setting->scope == HAGUI_SCOPE_GLOBAL ? "global" : "per_save") +
            ",\"type\":" + QuoteJson(TypeName(setting->inputType)) + ",\"enabled\":" +
            (setting->enabled ? "true" : "false") + ",\"value\":" + ValueJson(setting->value) + "}";
}

std::string RunSettingsOnMain(const std::vector<std::string>& args) {
    const auto host = ::GetModuleHandleW(L"HagLoader.dll");
    const auto getApi = reinterpret_cast<HagUI_GetAPIFn>(host ? ::GetProcAddress(host, "HagUI_GetAPI") : nullptr);
    const HagUIAPI* api = getApi ? getApi(10) : nullptr;
    if (!api || !api->VisitSettings || !api->SetSetting) return "err HagLoader settings API unavailable";

    const bool list = args[0] == "list";
    const char* page = args.size() >= 2 ? args[1].c_str() : nullptr;
    const char* id = args.size() >= 3 ? args[2].c_str() : nullptr;
    if (args[0] == "set") {
        int32_t inputType = -1;
        const auto status = api->VisitSettings(page, id, [](void* user, const HagUI_Setting* setting) {
            *static_cast<int32_t*>(user) = setting->inputType;
        }, &inputType);
        if (status != HAGUI_SETTING_OK) return Error(status);
        HagUI_Value value{};
        if (!ParseInput(args[3], inputType, value)) return Error(HAGUI_SETTING_INVALID);
        const auto applied = api->SetSetting(page, id, value);
        if (applied != HAGUI_SETTING_OK) return Error(applied);
    }

    std::string json;
    const auto status = api->VisitSettings(page, id, &AppendSetting, &json);
    if (status != HAGUI_SETTING_OK) return Error(status);
    return list ? "ok [" + json + "]" : "ok " + json;
}

}  // namespace

std::string RunSettingsCommand(const std::string& line) {
    if (line.size() > 2048 || line.find('\0') != std::string::npos) return kUsage;
    std::istringstream input(line);
    std::string word;
    input >> word;  // settings
    std::vector<std::string> args;
    while (input >> std::ws && !input.eof()) {
        if (!(input >> std::quoted(word)) || args.size() >= 4) return kUsage;
        args.push_back(word);
    }
    if (args.empty() || !((args[0] == "list" && args.size() <= 2) ||
                          (args[0] == "get" && args.size() == 3) ||
                          (args[0] == "set" && args.size() == 4))) return kUsage;

    auto response = std::make_shared<std::string>("err settings request failed");
    if (!mt::RunOnMain([response, args = std::move(args)] { *response = RunSettingsOnMain(args); }, 1500)) {
        return "err settings needs the game main thread (load a save and focus Skyrim)";
    }
    return *response;
}

}  // namespace hag::ipc
