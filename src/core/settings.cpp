#include "lumen/Settings.h"
#include <windows.h>
#include <cwchar>
#include <string>

namespace lumen {
namespace {

HKEY OpenKey(const std::wstring& subkey, bool write) {
    HKEY key = nullptr;
    const REGSAM access = KEY_QUERY_VALUE | (write ? KEY_SET_VALUE : 0);
    if (write) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, access, nullptr,
                            &key, nullptr) != ERROR_SUCCESS) {
            return nullptr;
        }
        return key;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, access, &key) != ERROR_SUCCESS) {
        return nullptr;
    }
    return key;
}

std::wstring ReadString(HKEY key, std::wstring_view name, std::wstring_view fallback) {
    std::wstring nm(name);
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, nm.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        type != REG_SZ || bytes < sizeof(wchar_t)) {
        return std::wstring(fallback);
    }
    std::wstring out(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, nm.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(out.data()),
                         &bytes) != ERROR_SUCCESS) {
        return std::wstring(fallback);
    }
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

} // namespace

Settings::Settings(std::wstring_view subkey) : subkey_(subkey) {}

std::wstring Settings::String(std::wstring_view name, std::wstring_view fallback) const {
    HKEY key = OpenKey(subkey_, false);
    if (!key) return std::wstring(fallback);
    std::wstring out = ReadString(key, name, fallback);
    RegCloseKey(key);
    return out;
}

int Settings::Int(std::wstring_view name, int fallback) const {
    const std::wstring text = String(name);
    if (text.empty()) return fallback;
    return _wtoi(text.c_str());
}

bool Settings::Bool(std::wstring_view name, bool fallback) const {
    return Int(name, fallback ? 1 : 0) != 0;
}

double Settings::Double(std::wstring_view name, double fallback) const {
    const std::wstring text = String(name);
    if (text.empty()) return fallback;
    return _wtof(text.c_str());
}

Settings& Settings::Put(std::wstring_view name, std::wstring_view value) {
    HKEY key = OpenKey(subkey_, true);
    if (!key) return *this;
    std::wstring nm(name);
    std::wstring text(value);
    const DWORD bytes = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(key, nm.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()), bytes);
    RegCloseKey(key);
    return *this;
}

Settings& Settings::Put(std::wstring_view name, int value) {
    const std::wstring text = std::to_wstring(value);
    return Put(name, std::wstring_view(text));
}

Settings& Settings::Put(std::wstring_view name, bool value) {
    return Put(name, value ? 1 : 0);
}

Settings& Settings::Put(std::wstring_view name, double value) {
    const std::wstring text = std::to_wstring(value);
    return Put(name, std::wstring_view(text));
}

void Settings::Persist(Property<float>& p, std::wstring_view name) {
    const std::wstring key(name);
    p = static_cast<float>(Double(key, static_cast<double>(p.Get())));
    persist_.push_back(ScopedConnection(p.OnChanged(
        [this, key](const float& v) { Put(key, static_cast<double>(v)); })));
}

void Settings::Persist(Property<int>& p, std::wstring_view name) {
    const std::wstring key(name);
    p = Int(key, p.Get());
    persist_.push_back(ScopedConnection(p.OnChanged([this, key](const int& v) { Put(key, v); })));
}

void Settings::Persist(Property<bool>& p, std::wstring_view name) {
    const std::wstring key(name);
    p = Bool(key, p.Get());
    persist_.push_back(ScopedConnection(p.OnChanged([this, key](const bool& v) { Put(key, v); })));
}

void Settings::Persist(Property<std::wstring>& p, std::wstring_view name) {
    const std::wstring key(name);
    p = String(key, p.Get());
    persist_.push_back(ScopedConnection(
        p.OnChanged([this, key](const std::wstring& v) { Put(key, std::wstring_view(v)); })));
}

} // namespace lumen
