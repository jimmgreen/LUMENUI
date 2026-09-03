// lumen/Settings.h — HKCU 注册表读写。路径形如 L"Software\\App"。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Signal.h"
#include <string>
#include <string_view>
#include <vector>

namespace lumen {

class Settings {
public:
    explicit Settings(std::wstring_view subkey);

    std::wstring String(std::wstring_view name, std::wstring_view fallback = {}) const;
    int Int(std::wstring_view name, int fallback = 0) const;
    bool Bool(std::wstring_view name, bool fallback = false) const;
    double Double(std::wstring_view name, double fallback = 0.0) const;

    Settings& Put(std::wstring_view name, std::wstring_view value);
    Settings& Put(std::wstring_view name, int value);
    Settings& Put(std::wstring_view name, bool value);
    Settings& Put(std::wstring_view name, double value);

    void Persist(Property<float>& p, std::wstring_view name);
    void Persist(Property<int>& p, std::wstring_view name);
    void Persist(Property<bool>& p, std::wstring_view name);
    void Persist(Property<std::wstring>& p, std::wstring_view name);

private:
    std::wstring subkey_;
    std::vector<ScopedConnection> persist_;
};

} // namespace lumen
