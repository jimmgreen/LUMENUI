// lumen/Window.h — 顶层窗口：渲染、输入路由、焦点、动画时钟、主题跟随。
// Events: OnClosing / OnFrame / OnTrayClick / BindTrayClick
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Core.h"
#include "Dialog.h"
#include "Drawer.h"
#include "Icons.h"
#include "Menu.h"
#include "Text.h"
#include "Theme.h"
#include "App.h"
#include "Panel.h"
#include "Signal.h"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace lumen {

class Flyout;
class TeachingTip;
class Control;
class TitleBar;
class BusyOverlay;
class Command;

// 窗口背景装饰（LUMEN 氛围层）：36px 暗网格 + 顶部径向环境辉光。
enum class Backdrop { None, Grid, Vignette, All };

// 窗口边框：System 走 OS 标题栏；Client 客户区铺满，自绘 40 DIP 标题栏。
enum class Frame { System, Client };  // Client: self-drawn TitleBar (40 DIP).

// 右下角 Toast 退场。入场始终从槽位下方滑入；堆叠时最新一条贴底。
enum class ToastMotion {
    Fade,         // 淡出并微微上移
    SlideRight,   // 右移出客户区
    SlideDown,    // 下沉
    Scale,        // 收缩并淡出
};

// 语义靠亮度阶梯与字形，不引入彩色。Default 无自定义字形时画原来的强调圆点。
enum class ToastKind { Default, Info, Success, Warning, Error };

inline const wchar_t* ToastKindGlyph(ToastKind kind) noexcept {
    switch (kind) {
    case ToastKind::Success: return icon::kCheckMark;
    case ToastKind::Warning: return icon::kWarning;
    case ToastKind::Error: return icon::kShield;
    case ToastKind::Info: return icon::kInfo;
    default: return nullptr;
    }
}

struct ToastData {
    std::wstring text;
    std::wstring glyph;                 // 空则用 ToastKindGlyph；Default 且空则画圆点
    std::wstring action;                // 空则无操作钮
    std::function<void()> on_action;
    float duration = 2.4f;              // 停留秒数；<=0 直到点关闭或操作
    ToastKind kind = ToastKind::Default;
};

struct WindowSpec {
    std::wstring title;
    Size size{960.0f, 640.0f};
    Frame frame = Frame::Client;
    Backdrop backdrop = Backdrop::All;
};

class Window {
public:
    // 默认 LUMEN 外观：客户区自绘标题栏、Backdrop::All、960×640、MinSize 60%、exe 第一枚图标。
    explicit Window(std::wstring_view title);
    Window(WindowSpec spec);
    // 尺寸为客户区 DIP。三参保留给要系统边框/自定义尺寸的人（默认 Frame::System，不自动 Backdrop/图标）。
    Window(std::wstring_view title, Size client_size, Frame frame = Frame::System);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 根容器（纵向 Column）。横向用 Row，分栏用 Grid；装饰块才 SetBounds。
    StackPanel& Root();
    // Client frame only; System frame returns nullptr.
    class TitleBar* TitleBar();

    void Show();
    void Close();
    bool Closed() const;

    void Title(std::wstring_view text);
    void Resize(Size client_size);
    void MinSize(Size min_size);

    // 光效强度 0..1：全局缩放辉光/聚光 token。LUMEN 恒为暗色单色主题。
    void GlowIntensity(float intensity);
    float GlowIntensity() const;
    // 背景装饰层（画在背景色之上、控件之下）。
    lumen::Backdrop Backdrop() const;
    void Backdrop(lumen::Backdrop backdrop);
    // 当前生效主题（颜色 token 快照，随光效强度更新内容）。
    const Theme& VisualTheme() const;

    // 返回 false 可取消关闭。
    void OnClosing(std::function<bool()> callback);

    // 模态对话框（窗口内覆盖层）。同窗口一次只允许一个。
    void ShowDialog(Dialog& dialog);
    void ShowDialog(std::unique_ptr<Dialog> dialog);
    void ShowDialog(DialogSpec spec);
    void CloseDialog();
    bool DialogActive() const;
    void Confirm(std::wstring_view title, std::wstring_view message, std::function<void(bool)> then,
                 std::wstring_view ok = {}, std::wstring_view cancel = {});
    void Alert(std::wstring_view title, std::wstring_view message);
    void Prompt(std::wstring_view title, std::wstring_view message,
                std::function<void(std::optional<std::wstring>)> then,
                std::wstring_view placeholder = {});

    Control* FocusFirst();
    Control* FocusNext(bool backwards = false);
    Connection OnFrame(std::function<bool(float dt)> fn);

    void Icon(int resource_id);
    void Icon(std::wstring_view path_or_name);
    void Icon(std::span<const std::byte> ico);

    void DumpTree(std::wostream& out) const;

    void RunAsync(std::function<void()> work, std::function<void()> then);
    void RunAsync(std::function<void()> work, std::function<void()> then, std::wstring_view busy);
    template <class R>
    void RunAsync(std::function<R()> work, std::function<void(R)> then);
    template <class R>
    void RunAsync(std::function<R()> work, std::function<void(R)> then, std::wstring_view busy);

    // 轻量弹层（窗口内浮层，单实例）：锚定控件弹出任意内容，点窗外/Esc 轻触关闭。
    void ShowFlyout(Flyout& flyout, const Control* anchor);
    void CloseFlyout();
    bool FlyoutActive() const;

    // 带箭头的引导气泡（与 Flyout 共用 overlay 槽，同时只显示一个）。
    void ShowTeachingTip(TeachingTip& tip, const Control* anchor);

    // 忙碌遮罩：半透明碳底 + ProgressRing。持续动画仅在遮罩存在期间运行。
    void ShowBusy(std::wstring_view text, std::function<void()> on_cancel = {});
    void CloseBusy();
    bool BusyActive() const;

    // 贴边全高临时抽屉。点遮罩或 Esc 滑出关闭。
    void ShowDrawer(Drawer& drawer, Edge edge);
    void CloseDrawer();
    bool DrawerActive() const;

    // 从任意线程投递到窗口 UI 线程执行（内部 PostMessage）。
    void Post(std::function<void()> fn);
    bool IsUiThread() const;

    // 业务定时器（WM_TIMER，不抢 vsync 动画拍）。返回句柄，0 表示失败。
    using TimerId = std::uintptr_t;
    TimerId SetInterval(float seconds, std::function<void()> fn);
    TimerId SetTimeout(float seconds, std::function<void()> fn);
    void ClearTimer(TimerId id);

    // 窗口加速键。焦点在 IME 行内编辑且和弦无 Ctrl/Alt 时让行。
    void BindShortcut(std::wstring_view chord, std::function<void()> fn);
    void Bind(Command& command);

    // Show 前读、关闭时写 WINDOWPLACEMENT；越界回主屏。HKCU 路径，如 L"Software\\App\\Main"。
    void RememberPlacement(std::wstring_view registry_path);

    // 托盘：hicon 为 HICON（公共头不暴露该类型）；空则用默认应用图标。
    void TrayIcon(void* hicon, std::wstring_view tooltip);
    void TrayIcon(int resource_id, std::wstring_view tooltip);
    void TrayIcon(std::wstring_view path_or_name, std::wstring_view tooltip);
    void TrayIcon(std::span<const std::byte> ico, std::wstring_view tooltip);
    void OnTrayClick(std::function<void()> handler);
    Connection BindTrayClick(std::function<void()> handler);
    void MinimizeToTray(bool on = true);
    void TrayMenu(Menu menu);

    // 右下角操作通知（自动退场），可多条堆叠。
    void ShowToast(std::wstring_view text);
    void ShowToast(std::string_view utf8);
    void ShowToast(std::wstring_view text, ToastKind kind);
    void ShowToast(ToastData data);
    lumen::ToastMotion ToastMotion() const;
    void ToastMotion(lumen::ToastMotion motion);

    void Invalidate();

    // 测试/自动化：按客户区 DIP 注入指针与键盘，并立即做一次布局。
    void LayoutNow();
    void DispatchMouseMove(Point client_dip, uint32_t buttons = 0);
    void DispatchMouseDown(Point client_dip, uint32_t buttons = 1);
    void DispatchMouseUp(Point client_dip, uint32_t buttons = 1);
    // 触摸注入：走 WM_POINTER 同一套路径（8px 容差、拖动平移）。phase 由 Down/Move/Up 三个方法表达。
    void DispatchTouchDown(Point client_dip);
    void DispatchTouchMove(Point client_dip);
    void DispatchTouchUp(Point client_dip);
    bool DispatchKey(uint32_t vk);
    Control* Hovered() const;
    Control* Focused() const;

    void* NativeHandle() const;   // HWND

private:
    friend class Control;
    friend class Dialog;
    friend class Menu;
    friend class WindowImpl;
    class WindowImpl* Impl() const noexcept;

    std::unique_ptr<class WindowImpl> impl_;
};

template <class R>
void Window::RunAsync(std::function<R()> work, std::function<void(R)> then) {
    auto box = std::make_shared<std::optional<R>>();
    RunAsync([work = std::move(work), box] { *box = work(); },
             [then = std::move(then), box] {
                 if (then && box->has_value()) then(std::move(**box));
             });
}

template <class R>
void Window::RunAsync(std::function<R()> work, std::function<void(R)> then, std::wstring_view busy) {
    ShowBusy(busy);
    RunAsync(std::move(work), [this, then = std::move(then)](R value) {
        CloseBusy();
        if (then) then(std::move(value));
    });
}

template <class Fn>
int Run(std::wstring_view title, Fn&& build) {
    App app;
    Window window(title);
    build(window);
    window.Show();
    return app.Run();
}

} // namespace lumen
