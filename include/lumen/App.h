// lumen/App.h — 进程级初始化与消息循环。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Strings.h"
#include <cstdint>
#include <functional>
#include <string_view>

namespace lumen {

class App {
public:
    // 开启 Per-Monitor V2 DPI 感知并注册窗口类；进程内构造一次即可。
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run();                              // 标准消息循环
    static void Quit(int exit_code = 0);    // PostQuitMessage

    // 幂等：DPI 感知。Window 构造也会调用，免得忘了 App 对象。
    static void Ensure();

    static const Strings& Strings();
    static void Strings(lumen::Strings value);

    // 命名互斥。已有实例时激活其主窗并返回 false。
    static bool SingleInstance(std::wstring_view name);
    // 系统级热键（RegisterHotKey）。解析与 BindShortcut 相同。
    static bool RegisterGlobalHotkey(std::wstring_view chord, std::function<void()> fn);
    static void UnregisterGlobalHotkey(std::wstring_view chord);
};

} // namespace lumen
