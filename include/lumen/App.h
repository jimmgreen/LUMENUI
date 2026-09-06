// lumen/App.h — 进程级初始化与消息循环。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Strings.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
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

    // 宿主嵌入：lumen 被别的进程加载（AutoCAD .arx、Office/VS 插件等）时，在建第一个
    // 窗口前置 true。效果：不改进程 DPI 感知（改为每个窗口在 PMv2 线程上下文里创建）、
    // 不 EnableMouseInPointer（走 WM_MOUSE* 路径）、最后一个窗口关闭不 PostQuitMessage、
    // 无 App::Run（宿主自己泵消息，Window::Post 跨线程可用）。
    static void HostMode(bool on);
    static bool HostMode();
    // 卸载模块前调用（所有 Window 已析构）：注销窗口类、释放 DWrite/字体/布局缓存、
    // 释放消息窗口与全局热键，回到未 Ensure 状态；再次创建窗口会重新初始化。
    // 在UI/STA线程重试未完成的UIA断开；仍有 provider 引用或 RunAsync 任务未结束时
    // 返回 false——模块代码可能仍在后台线程执行，不能用「丢弃 UI 回调」代替任务结束。
    static bool CanShutdown();
    static void Shutdown();
    // 外部任务可成对使用 TaskBegin/TaskEnd；RunAsync 自行持有系统线程到完整退出。
    // RunningTasks 包含尚未退出的 RunAsync 线程（包括闭包/TLS 析构），并回收已退出线程。
    static void TaskBegin() noexcept;
    static void TaskEnd() noexcept;
    static int RunningTasks() noexcept;

    // 自定义字体：内存 TTF/OTF 字节（内部复制）或字体文件路径。返回首个族名，供
    // Label::FontFamily / RichLabel::Font 使用；失败返回空。LumaText 不认识的族名自动回退 DirectWrite。
    static std::wstring AddFont(std::span<const std::byte> data);
    static std::wstring AddFont(std::wstring_view path);

    // lumatext.dll 完整路径（默认：先找含 lumen 的模块所在目录，再按系统搜索顺序）。
    // 必须在第一个窗口创建前设置；找不到即整进程回退 DirectWrite。
    static void LumaTextLibrary(std::wstring_view path);

    static const Strings& Strings();
    static void Strings(lumen::Strings value);

    // 命名互斥。已有实例时激活其主窗并返回 false。
    static bool SingleInstance(std::wstring_view name);
    // 系统级热键（RegisterHotKey）。解析与 BindShortcut 相同。
    static bool RegisterGlobalHotkey(std::wstring_view chord, std::function<void()> fn);
    static void UnregisterGlobalHotkey(std::wstring_view chord);
};

} // namespace lumen
