// lumen/Window.h — 顶层窗口：渲染、输入路由、焦点、动画时钟、主题跟随。
// Events: OnClosing / OnFrame / OnTrayClick / BindTrayClick / OnNativeMessage / BindNativeMessage / OnTaskFailed / BindTaskFailed
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
#include "Dispatcher.h"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace lumen {

class Flyout;
class TeachingTip;
class Control;

// Post 的投递结果。Accepted：已入队且唤醒已提交（窗口未销毁就必然执行）；
// Closed：窗口已进入销毁流程，任务被拒绝且不会执行；WakeFailed：UI 线程唤醒
// 投递失败，任务已回滚——返回拒绝后绝不会执行，调用方应改走自己的重试路径。

// 后台任务终态。Dropped = 工作已结束但结果未能交付（窗口先销毁）；error 保留失败信息。
enum class TaskStatus { Running, CancelRequested, Succeeded, Failed, Dropped, Cancelled };
enum class TaskDelivery { Pending, Delivered, Dropped };
struct TaskCancelled {};

// TaskHandle 共享的内部状态；公共头仅为此暴露，勿直接使用。
struct TaskStatusBox {
    std::mutex mutex;
    TaskStatus status = TaskStatus::Running;
    TaskDelivery delivery = TaskDelivery::Pending;
    bool cancel_requested = false;
    std::wstring error;
};

// 协作式后台任务句柄：可拷贝共享观察，不拥有任务。Cancel 只是请求，
// work 内部轮询 CancelRequested 自行决定何时收尾（不承诺强制中止）。
class TaskHandle {
public:
    TaskHandle() = default;
    void Cancel() const {
        if (!state_) return;
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->status == TaskStatus::Running || state_->status == TaskStatus::CancelRequested) {
            state_->cancel_requested = true;
            state_->status = TaskStatus::CancelRequested;
        }
    }
    bool CancelRequested() const {
        if (!state_) return false;
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->cancel_requested;
    }
    void ThrowIfCancelled() const { if (CancelRequested()) throw TaskCancelled{}; }
    TaskDelivery Delivery() const {
        if (!state_) return TaskDelivery::Dropped;
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->delivery;
    }
    TaskStatus Status() const {
        if (!state_) return TaskStatus::Dropped;
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->status;
    }
    // Failed 时的异常消息（what() 转宽），其余终态为空。
    std::wstring Error() const {
        if (!state_) return {};
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->error;
    }
    explicit operator bool() const noexcept { return state_ != nullptr; }

private:
    friend class Window;
    friend class WindowImpl;
    explicit TaskHandle(std::shared_ptr<TaskStatusBox> state) noexcept
        : state_(std::move(state)) {}
    std::shared_ptr<TaskStatusBox> state_;
};
class TitleBar;
class BusyOverlay;
class Command;

// 窗口背景装饰（LUMEN 氛围层）：36px 暗网格 + 顶部径向环境辉光。
enum class Backdrop { None, Grid, Vignette, All };

// 窗口边框：System 走 OS 标题栏；Client 客户区铺满，自绘 40 DIP 标题栏。
enum class Frame { System, Client };  // Client: self-drawn TitleBar (40 DIP).

// Toast 停靠角：牌堆锚定该边——最新一条完整贴住锚边，更早的折叠层沿远离锚边的方向
// 露出上沿（底部停靠向上露、顶部停靠向下露），最多露 3 层；hover 牌堆整堆展开为
// 纵向列表，移开重新折叠，悬停期间停留计时暂停。
enum class ToastPlacement {
    BottomRight, BottomCenter, BottomLeft,
    TopRight, TopCenter, TopLeft,
};

// 入场始终从槽位外侧滑入（底部停靠自下滑入、顶部停靠自上滑入）；退场方向见下
// （折叠在背后的层退场为原地淡出、缩回前卡后面）。
enum class ToastMotion {
    Fade,         // 淡出并微微上移
    SlideRight,   // 右移出客户区
    SlideDown,    // 下沉
    Scale,        // 收缩并淡出
};
enum class MotionMode { System, Full, Reduced, Off };

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
    std::wstring title;
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
    // false：不下自绘标题栏（无标题/关闭），Win32 仅 WS_POPUP。用于色板/下拉类顶层小窗。
    bool titleBar = true;
    // 所有者窗口 HWND（公共头不暴露该类型）。非空：Z 序压在其上、随其最小化、Client 帧居中于其上。
    // 宿主嵌入（AutoCAD 主框架等）用；模态禁用/恢复由调用方自己做。
    void* owner = nullptr;
    // 非空：按该 HWND 的 DPI 感知上下文建窗（供 SetParent 嵌入；避免 ERROR_INVALID_STATE）。
    void* matchDpiHwnd = nullptr;
    // 非空：直接创建 WS_CHILD，DPI 匹配父窗；NativeHandle 始终返回此子窗。
    void* parent = nullptr;
    // 嵌入时可指定外壳：Client 标题栏动作与 Resize 路由此外壳，外壳负责布局、关闭与阴影。
    // 外壳 WM_GETMINMAXINFO 转发给子窗以应用 MinSize；键盘预处理须让子窗及其 IME 消息通过。
    void* frameTarget = nullptr;
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
    UiDispatcher Dispatcher() const;
    void Hide();
    bool Visible() const;
    bool Activated() const;
    void OnShowing(std::function<void()> fn) { showing_.Subscribe(std::move(fn)); }
    Connection BindShowing(std::function<void()> fn) { return showing_.Connect(std::move(fn)); }
    void OnShown(std::function<void()> fn) { shown_.Subscribe(std::move(fn)); }
    Connection BindShown(std::function<void()> fn) { return shown_.Connect(std::move(fn)); }
    void OnHidden(std::function<void()> fn) { hidden_.Subscribe(std::move(fn)); }
    Connection BindHidden(std::function<void()> fn) { return hidden_.Connect(std::move(fn)); }
    void OnActivated(std::function<void(bool)> fn) { activated_.Subscribe(std::move(fn)); }
    Connection BindActivated(std::function<void(bool)> fn) { return activated_.Connect(std::move(fn)); }
    void OnDestroyed(std::function<void()> fn) { destroyed_.Subscribe(std::move(fn)); }
    Connection BindDestroyed(std::function<void()> fn) { return destroyed_.Connect(std::move(fn)); }
    // AutoFit 仅在首次 Show 前适配；之后保留用户尺寸。FitContent 为显式重新适配。
    void AutoFit(float preferred_width) { fit_width_ = preferred_width; fit_pending_ = true; }
    void FitContent(float preferred_width);
    void Close();
    bool Closed() const;

    void Title(std::wstring_view text);
    void Resize(Size client_size);
    // 仅 UI 线程：按指定客户区宽度（DIP）测量自然内容尺寸，高度包含可见标题栏。
    // 不改变窗口尺寸/显示或控件位置；下次正常布局重新测量。无效宽度返回空尺寸。
    Size MeasureContent(float client_width);
    void MinSize(Size min_size);

    // 光效强度 0..1：全局缩放辉光/聚光 token。LUMEN 恒为暗色单色主题。
    void GlowIntensity(float intensity);
    float GlowIntensity() const;
    // Client 帧标题栏右侧的帧耗时 HUD（FPS / 绘制 / 呈现 / 脏区 / 工作集）。默认关；
    // 开启后占用 TitleBar::Status 文案。
    void PerfHud(bool on);
    bool PerfHud() const;
    // 背景装饰层（画在背景色之上、控件之下）。
    lumen::Backdrop Backdrop() const;
    void Backdrop(lumen::Backdrop backdrop);
    // 当前生效主题（颜色 token 快照，随光效强度更新内容）。
    const Theme& VisualTheme() const;
    void Motion(MotionMode mode);
    MotionMode Motion() const;

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
    // 清掉当前逻辑焦点（HasFocus / 焦点环 / 插入符）。不改 Win32 焦点；
    // 要把键盘还给宿主，调用方再 SetFocus(owner)。显式清除后，下次 WM_SETFOCUS 不恢复。
    void ClearFocus();
    Connection OnFrame(std::function<bool(float dt)> fn);
    // 原生 WndProc 观察：在 lumen 默认处理之前发出。msg/wparam/lparam 与 Win32 同宽，
    // 公共头不引入 windows.h。高频消息（移动/绘制）也会到，订阅方自行过滤。
    // 不要在回调里泵消息或 DestroyWindow。
    void OnNativeMessage(
        std::function<void(uint32_t msg, std::uintptr_t wparam, std::intptr_t lparam)> fn);
    Connection BindNativeMessage(
        std::function<void(uint32_t msg, std::uintptr_t wparam, std::intptr_t lparam)> fn);

    void Icon(int resource_id);
    void Icon(std::wstring_view path_or_name);
    void Icon(std::span<const std::byte> ico);

    void DumpTree(std::wostream& out) const;

    // 后台线程跑 work，完成后经投递端口回 UI 线程调 then（窗口已销毁则结果按
    // Dropped 收尾）。work 抛异常进入 Failed：不调 then，异常消息经 OnTaskFailed
    // 报告并写日志；带 busy 的重载在成功/失败/丢弃所有分支都收掉忙碌遮罩。
    // 返回句柄可 Cancel（协作）与观察 Status；work 内轮询取消见 TaskHandle。
    TaskHandle RunAsync(std::function<void()> work, std::function<void()> then);
    TaskHandle RunAsync(std::function<void(const TaskHandle&)> work, std::function<void()> then,
                        std::wstring_view busy = {});
    TaskHandle RunAsync(std::function<void()> work, std::function<void()> then,
                        std::wstring_view busy);
    template <class R>
    TaskHandle RunAsync(std::function<R()> work, std::function<void(R)> then);
    template <class R>
    TaskHandle RunAsync(std::function<R()> work, std::function<void(R)> then,
                        std::wstring_view busy);
    // RunAsync 异常的报告通道（UI 线程）。不订阅也安全：异常始终写日志。
    Window& OnTaskFailed(std::function<void(std::wstring_view message)> fn) {
        task_failed_.Subscribe(std::move(fn));
        return *this;
    }
    Connection BindTaskFailed(std::function<void(std::wstring_view message)> fn) {
        return task_failed_.Connect(std::move(fn));
    }

    // 轻量弹层（窗口内浮层，单实例）：锚定控件弹出任意内容，点窗外/Esc 轻触关闭。
    void ShowFlyout(Flyout& flyout, const Control* anchor);
    void CloseFlyout();
    bool FlyoutActive() const;

    // 通用弹出窗（R06）：内容是任意控件子树，越过窗口客户区（色板/取色器/迷你面板）。
    // 锚定控件弹出，放不下自动翻上方，钳进屏幕工作区。模态泵（与菜单同通道）：
    // 阻塞至收起，期间拦截内容区输入，主窗仍可移动；外点 / Esc / ClosePopup() 收起。
    // 每个 UI 线程仅一个会话，重入 ShowPopup 被忽略。content 由调用方持有且不能已挂载。
    // closed 在收起后于 UI 线程调用；owner 销毁时不再调用，避免访问失效的捕获对象。
    void ShowPopup(Control& content, const Control* anchor, float width = 260.0f,
                   std::function<void()> closed = {});
    void ClosePopup();
    bool PopupActive() const;

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

    // 从任意线程投递到窗口 UI 线程执行（内部 PostMessage）。返回投递结果：
    // 拒绝（Closed/WakeFailed）时任务保证不会执行。同线程投递同样排队，不内联。
    PostResult Post(std::function<void()> fn);
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

    // 右下角操作通知（自动退场），可多条堆叠；停靠位与边距可配。
    void ShowToast(std::wstring_view text);
    void ShowToast(std::string_view utf8);
    void ShowToast(std::wstring_view text, ToastKind kind);
    void ShowToast(ToastData data);
    lumen::ToastMotion ToastMotion() const;
    void ToastMotion(lumen::ToastMotion motion);
    // 停靠角（默认 BottomRight）。已有可见牌堆时平滑飞往新角落。
    lumen::ToastPlacement ToastPlacement() const;
    void ToastPlacement(lumen::ToastPlacement placement);
    // 距左右边与锚边的 DIP（顶部停靠 = 自绘标题栏下沿起算），默认 20，下限 8。
    float ToastMargin() const;
    void ToastMargin(float margin);

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
    Signal<> showing_, shown_, hidden_, destroyed_;
    Signal<bool> activated_;
    float fit_width_ = 640.0f;
    bool fit_pending_ = false;
    Signal<std::wstring_view> task_failed_;   // RunAsync 异常（WindowImpl 在 UI 线程 Emit）
};

template <class R>
TaskHandle Window::RunAsync(std::function<R()> work, std::function<void(R)> then) {
    auto box = std::make_shared<std::optional<R>>();
    return RunAsync([work = std::move(work), box] { *box = work(); },
                    [then = std::move(then), box] {
                        if (then && box->has_value()) then(std::move(**box));
                    });
}

template <class R>
TaskHandle Window::RunAsync(std::function<R()> work, std::function<void(R)> then,
                            std::wstring_view busy) {
    ShowBusy(busy);
    return RunAsync(std::move(work), [this, then = std::move(then)](R value) {
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
