// app_host.h — App 与 WindowImpl 之间的进程级通道（单实例 / 全局热键 / 窗口类 / 宿主模式）。
#pragma once
#include <windows.h>
#include <functional>

namespace lumen {

void AppBindWindow(HWND hwnd);
void AppUnbindWindow(HWND hwnd);
void AppActivateExisting();
bool AppHandleHotkey(WPARAM id);
UINT AppActivateMsg();

// 保留线程所有权，卸载门禁必须观察系统线程退出（包含闭包与 TLS 析构）。
void AppStartWorker(std::function<void()> work);

// 含 lumen 代码的模块（exe 或被宿主加载的 DLL/.arx）。窗口类与资源都挂在它上面，
// 不能用 GetModuleHandleW(nullptr)：宿主进程里那是宿主 exe。
HINSTANCE LumenModule();

// 三个窗口类统一注册。同名陈旧类（上一次 DLL 卸载未 Shutdown）先注销再注册，
// 否则 CreateWindow 会拿到指向已卸载代码的 WndProc。
enum class LumenClass { Window, App, Menu, Popup };
const wchar_t* LumenClassName(LumenClass cls) noexcept;
bool EnsureLumenClass(LumenClass cls, WNDPROC proc, UINT style, bool arrow_cursor);
void UnregisterLumenClasses();

// 宿主模式下窗口默认在 PMv2 上下文里创建（进程可能只是 System Aware）；
// 消息分发时系统会自动切回窗口的上下文，构造期外的调用用缓存的 scale_。
// 嵌入宿主子窗时传入 match_hwnd，改用该窗的 DPI 感知，否则 SetParent 会 ERROR_INVALID_STATE。
class DpiContextScope {
public:
    explicit DpiContextScope(HWND match_hwnd = nullptr);
    ~DpiContextScope();
    DpiContextScope(const DpiContextScope&) = delete;
    DpiContextScope& operator=(const DpiContextScope&) = delete;

private:
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

} // namespace lumen
