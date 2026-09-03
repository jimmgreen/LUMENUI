// window_impl.h — Window 的内部实现：Win32 窗口、渲染、输入、焦点、动画。
#pragma once
#include "lumen/Window.h"
#include "lumen/Signal.h"
#include "lumen/BusyOverlay.h"
#include "lumen/Dialog.h"
#include "lumen/Drawer.h"
#include "lumen/Menu.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "com_ptr.h"
#include "renderer.h"
#include "text_service.h"
#include <windows.h>
#include <d2d1_3.h>
#include <shellapi.h>
#include <deque>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace lumen {

inline constexpr UINT kWmTray = WM_APP + 0x21;

class Dialog;
class Flyout;
class TeachingTip;
class ToolTip;
class MenuWindow;
class TitleBar;
class BusyOverlay;
class Drawer;
class MenuBar;

class WindowImpl {
public:
    WindowImpl(Window* api, std::wstring_view title, Size client_size, Frame frame);
    ~WindowImpl();

    // 控件基类经 Window API 转发的内部通道。
    static void Invalidate(Window* window) {
        if (auto* impl = window ? window->Impl() : nullptr) impl->Invalidate();
    }
    static void InvalidateRegion(Window* window, const Rect& dip) {
        if (auto* impl = window ? window->Impl() : nullptr) impl->InvalidateRegion(dip);
    }
    static void Animate(Window* window, Control* control = nullptr) {
        if (auto* impl = window ? window->Impl() : nullptr) impl->RequestAnimation(control);
    }
    static void Relayout(Window* window) {
        if (auto* impl = window ? window->Impl() : nullptr) impl->RequestRelayout();
    }
    static void SetFocusTo(Window* window, Control* control) {
        if (auto* impl = window ? window->Impl() : nullptr) impl->SetFocusControl(control);
    }
    static void SyncImeCaret(Window* window) {
        if (auto* impl = window ? window->Impl() : nullptr) impl->SyncImeCaret();
    }
    // 子树整体写入 window_（脱离窗口构建的子树在挂载时补绑定）。
    static void BindWindowRecursive(Control* tree, Window* window);
    // 控件子树从窗口移除时清掉窗口留存的原始指针（焦点/悬停/捕获等）。
    static void ForgetTree(Control* tree);
    static HWND HwndOf(Window* window) { return window->Impl()->Hwnd(); }
    static float ScaleOf(Window* window) { return window->Impl()->Scale(); }
    static LumaTextBridge* LumaOf(Window* window);
    static bool HitTestBody(Window* window, std::wstring_view text, float x_dip, size_t* index,
                            TextRole role = TextRole::Body);
    static bool CaretXBody(Window* window, std::wstring_view text, size_t index, float* x_dip,
                           TextRole role = TextRole::Body);
    static DWORD DragUnicodeText(std::wstring_view text);
    static Theme& ThemeOf(Window* window) { return window->Impl()->ThemeRef(); }
    static bool KeyboardFocusOf(Window* window) {
        return window && window->Impl() && window->Impl()->keyboard_focus_;
    }
    static bool TouchInputOf(Window* window) {
        return window && window->Impl() && window->Impl()->touch_input_;
    }
    static void ShowTransient(Window* window, Control* overlay, const Control* anchor,
                              float width, bool prefer_above,
                              std::function<void()> closed = {});
    static void CloseTransient(Window* window);
    static bool TransientActive(Window* window, const Control* overlay = nullptr);

    void Show();
    void Close();
    bool Closed() const noexcept { return closed_; }
    void Title(std::wstring_view text);
    void Resize(Size client_size);
    void MinSize(Size min_size);
    void Invalidate();
    void InvalidateRegion(const Rect& dip);
    void RequestAnimation(Control* control = nullptr);
    void RequestRelayout();
    void SetFocusControl(Control* control);
    void GlowIntensity(float intensity);
    void SetBackdrop(Backdrop backdrop);
    void ShowDialog(Dialog& dialog);
    void ShowDialog(std::unique_ptr<Dialog> dialog);
    void ShowDialog(DialogSpec spec);
    Connection OnFrame(std::function<bool(float dt)> fn);
    void DisconnectFrame(uint64_t id);
    void SetIcon(int resource_id);
    void SetIcon(std::wstring_view path_or_name);
    void SetIconMemory(std::span<const std::byte> ico);
    void LoadFirstExeIcon();
    void DumpTree(std::wostream& out) const;
    void RunWorker(std::function<void()> work, std::function<void()> then);
    void CloseDialog();
    void FinishDialog();
    void ForgetControl(const Control* control);
    void OnToolTipChanged(Control* host);
    void ShowFlyout(Flyout& flyout, const Control* anchor);
    void ShowTeachingTip(TeachingTip& tip, const Control* anchor);
    void CloseFlyout(bool invoke_closed = true);
    bool FlyoutActive() const noexcept { return active_flyout_ != nullptr; }
    void ShowBusy(std::wstring_view text, std::function<void()> on_cancel);
    void CloseBusy();
    bool BusyActive() const noexcept { return active_busy_ != nullptr; }
    void ShowDrawer(Drawer& drawer, Edge edge);
    void RequestCloseDrawer();
    void FinishDrawer();
    bool DrawerActive() const noexcept { return active_drawer_ != nullptr; }
    void Post(std::function<void()> fn);
    bool IsUiThread() const noexcept;
    Window::TimerId SetInterval(float seconds, std::function<void()> fn, bool once);
    void ClearTimer(Window::TimerId id);
    void BindShortcut(std::wstring_view chord, std::function<void()> fn);
    void RememberPlacement(std::wstring_view registry_path);
    void TrayIcon(void* hicon, std::wstring_view tooltip);
    void OnTrayClick(std::function<void()> handler);
    Connection BindTrayClick(std::function<void()> handler);
    void MinimizeToTray(bool on);
    void SetTrayMenu(Menu menu);
    // Dialog/Flyout 析构时注销，避免窗口仍持有已销毁的 overlay 指针。
    static void OverlayDestroyed(Window* window, const Control* control);
    static void FinishDrawer(Window* window);
    static void FinishDialog(Window* window);
    void ShowToast(std::wstring_view text);
    void ShowToast(ToastData data);
    void SetToastMotion(ToastMotion motion);
    ToastMotion GetToastMotion() const noexcept { return toast_motion_; }
    void DrawToasts(Painter& painter, const Theme& theme, const Rect& client);
    void DrawTooltip(Painter& painter, const Theme& theme, const Rect& client);
    void* NativeHandle() const noexcept { return hwnd_; }
    StackPanel& Root() noexcept { return *root_; }
    TitleBar* TitleBarPtr() noexcept { return title_bar_.get(); }
    Theme& ThemeRef() noexcept { return theme_; }
    float Scale() const noexcept { return scale_; }
    HWND Hwnd() const noexcept { return hwnd_; }
    Control* Hovered() const noexcept { return hovered_; }
    Control* Focused() const noexcept { return focused_; }
    void LayoutNow();
    void DispatchMouseMove(Point client_dip, uint32_t buttons);
    void DispatchMouseButton(Point client_dip, uint32_t buttons, bool down, uint32_t changed);
    bool DispatchKey(uint32_t vk);

    LRESULT UiaGetObject(WPARAM wparam, LPARAM lparam);
    void UiaShutdown();
    void UiaOnFocus();
    void UiaForget(const Control* control);
    Control* UiaParentOf(const Control* c) const noexcept {
        return c ? c->parent_ : nullptr;
    }
    bool UiaFocusable(const Control* c) const noexcept {
        return c && c->Focusable() && c->enabled_;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void Paint();
    void DrawBackdrop(const Rect& client);
    void BlitBackdrop(ID2D1DeviceContext2* dc, const Rect& client);
    void DrawCaption(const Rect& client);
    void ApplyClientChrome();
    void AdjustFrameRect(RECT* rect) const;
    DWORD FrameStyle() const;
    float CaptionHeight() const noexcept;
    LRESULT HitTestFrame(LPARAM lparam) const;
    LRESULT CaptionButtonAt(POINT client_px) const;
    void SetCaptionHover(LRESULT hit);
    void TrackNcMouse();
    void UpdatePerfHud(float draw_ms, float present_ms, float frame_ms, bool full_present,
                       UINT dirty_n);
    void Layout();
    void LayoutFlyout();
    void DrawTree(Control* control);
    void AddDirtyRect(Rect dip);
    void RequestPaint();
    RECT DirtyPixelRect(const Rect& dip) const noexcept;
    void UpdateClientSize();
    void RefreshTheme();
    bool TickAnimations();
    void ArmAcrylic(bool tween);
    void ClearAcrylic();
    bool OverlayWantsAcrylic() const noexcept;
    float AcrylicAmount() const noexcept;
    float AcrylicDim() const noexcept;
    void CollectFocusable(Control* tree, std::vector<Control*>& order);
    static Point ToLocal(const Control* control, Point absolute);
    Control* HitTest(Point p);
    Control* HitTree(Control* control, Point p) const;
    Control* HitTree(Control* control, Point p, float slop) const;
    void OnMouseMove(int px, int py, uint32_t buttons);
    void SyncSpotlights(Point p, bool inside_window);
    void OnMouseButton(int px, int py, uint32_t buttons, bool down, uint32_t changed);
    bool OnPointer(UINT msg, WPARAM wparam, LPARAM lparam);
    void HandlePointerClient(int px, int py, DWORD type, int phase, uint32_t buttons,
                             uint32_t changed);
    void TryBeginPan(int px, int py);
    void ApplyPanMove(int px, int py);
    bool LegacyMouseFromPointer() const;
    void DispatchTouch(Point client_dip, int phase);
    bool OnKeyDown(uint32_t vk);
    bool TryShortcuts(uint32_t vk);
    bool FireMenuShortcuts(const std::vector<MenuItem>& items, uint32_t vk);
    void ScanMenuBarShortcuts(Control* tree, uint32_t vk, bool& hit);
    void DrainPosted();
    void FireTimer(UINT_PTR id);
    void ApplyPlacement();
    void SavePlacement();
    void InstallTray();
    void RemoveTray();
    void RestoreFromTray();
    void SyncImeCaret();
    bool ImeClientCaret(POINT* caret_px, int* line_h_px, RECT* doc_px) const;
    bool OnImeRequest(WPARAM wparam, LPARAM lparam, LRESULT* result);
    void HandleImeComposition(LPARAM lparam);
    void TrackMouse();
    static void EnsureWindowClass();
    Control* FindFirstFocusable();
    Control* FindNextFocusable(Control* current);
    Control* FindPrevFocusable(Control* current);
    void EnsureFocusVisible();
    void RegisterOleDrop();
    void UnregisterOleDrop();
    Control* FileDropAt(Point client_dip);
    Control* TextDropAt(Point client_dip);
    void SetDropArmed(Control* zone);
    Point DipFromScreen(long screen_x, long screen_y) const;

    Window* api_;
    HWND hwnd_ = nullptr;
    Frame frame_ = Frame::System;
    float scale_ = 1.0f;
    int client_w_ = 0, client_h_ = 0;   // 物理像素
    Size min_size_dip_{0.0f, 0.0f};
    std::wstring title_;

    Renderer renderer_;
    Painter painter_;
    Theme theme_;
    float glow_intensity_ = 0.5f;
    Backdrop backdrop_ = Backdrop::None;
    ComPtr<ID2D1Bitmap1> backdrop_cache_;
    bool backdrop_cache_dirty_ = true;
    Tween acrylic_tween_{};

    enum class CaptionHover { None, Min, Max, Close };
    CaptionHover caption_hover_ = CaptionHover::None;
    bool tracking_nc_mouse_ = false;
    wchar_t perf_hud_[96]{};
    float fps_ema_ = 0.0f;
    LARGE_INTEGER qpc_freq_{};
    LARGE_INTEGER last_hud_qpc_{};

    std::unique_ptr<StackPanel> root_;
    std::unique_ptr<TitleBar> title_bar_;
    Control* hovered_ = nullptr;
    Control* captured_ = nullptr;
    Control* focused_ = nullptr;
    bool keyboard_focus_ = false;
    bool touch_input_ = false;
    float hit_slop_dip_ = 0.0f;
    LONG pointer_msg_time_ = 0;
    UINT32 pointer_id_ = 0;
    bool panning_ = false;
    Control* pan_target_ = nullptr;
    POINT pan_origin_px_{};
    POINT pan_last_px_{};
    LARGE_INTEGER pan_last_qpc_{};
    float pan_vx_ = 0.0f;
    float pan_vy_ = 0.0f;
    Control* dialog_focus_return_ = nullptr;   // 对话框打开前的焦点，关闭时恢复
    Dialog* active_dialog_ = nullptr;
    Control* active_flyout_ = nullptr;      // 窗口内 transient overlay（单实例，非模态）
    Control* flyout_anchor_ = nullptr;
    float flyout_width_ = 260.0f;
    bool flyout_prefer_above_ = false;
    Control* flyout_focus_return_ = nullptr;
    std::function<void()> flyout_closed_;
    std::unique_ptr<BusyOverlay> owned_busy_;
    BusyOverlay* active_busy_ = nullptr;
    Control* busy_focus_return_ = nullptr;
    Drawer* active_drawer_ = nullptr;
    Control* drawer_focus_return_ = nullptr;

    DWORD ui_thread_id_ = 0;
    std::mutex post_mutex_;
    std::deque<std::function<void()>> post_queue_;
    struct TimerSlot {
        UINT_PTR id = 0;
        std::function<void()> fn;
        bool once = false;
    };
    std::vector<TimerSlot> timers_;
    UINT_PTR next_timer_id_ = 0x1100;
    struct Shortcut {
        std::wstring chord;
        std::function<void()> fn;
    };
    std::vector<Shortcut> shortcuts_;
    std::wstring placement_key_;
    bool tray_installed_ = false;
    bool minimize_to_tray_ = false;
    NOTIFYICONDATAW tray_{};
    Signal<> tray_click_;
    Menu tray_menu_;
    bool has_tray_menu_ = false;
    struct Toast {
        std::wstring text;
        std::wstring glyph;
        std::wstring action;
        std::function<void()> on_action;
        ToastKind kind = ToastKind::Default;
        float hold_seconds = 2.4f;   // <=0 持久
        double born_seconds = 0.0;
        double pause_seconds = 0.0;
        double exit_start = 0.0;
        float y = 0.0f;
        bool placed = false;
        bool exiting = false;
        bool hovering = false;
        bool action_hot = false;
        bool close_hot = false;
        Rect card{};
        Rect action_rect{};
        Rect close_rect{};
    };
    std::vector<Toast> toasts_;
    double toast_tick_ = 0.0;
    ToastMotion toast_motion_ = ToastMotion::Fade;
    ptrdiff_t toast_press_ = -1;
    int toast_press_part_ = 0;   // 0 card / 1 action / 2 close
    double clock_seconds() const;
    bool TickToasts(double now_seconds);
    float ToastHeight(const Toast& toast) const noexcept;
    const wchar_t* ToastGlyph(const Toast& toast) const noexcept;
    bool ToastPersist(const Toast& toast) const noexcept;
    enum class ToastPart { None, Card, Action, Close };
    ToastPart HitToast(Point p, ptrdiff_t* index) const;
    void BeginToastExit(Toast& toast, double now);
    void ClearToastHover();
    bool UpdateToastHover(Point p);

    // ToolTip：overlay 绘制，静置 ~600ms 淡入；移动/离开/点击/滚轮即收。
    Control* tooltip_control_ = nullptr;   // 悬停且带提示文本的目标（空 = 无跟踪）
    Control* tooltip_suppressed_ = nullptr;  // X 关闭后，离开该控件前不再弹出
    std::wstring tooltip_text_;            // 显示时刻快照，淡出期间目标变更不影响
    ToolTip* tooltip_custom_ = nullptr;    // 显示时刻的自定义内容（宿主持有）
    Control* tooltip_hover_ = nullptr;     // 自定义内容里当前悬停的子级
    Rect tooltip_anchor_bounds_{};         // 显示时刻目标矩形（窗口 DIP）
    double tooltip_dwell_start_ = 0.0;
    double tooltip_born_ = 0.0;
    double tooltip_fade_start_ = 0.0;
    Point tooltip_last_pos_{};
    bool tooltip_shown_ = false;
    bool tooltip_fading_ = false;
    Rect tooltip_bubble_{};            // 圆角卡片（不含箭头），供命中
    Rect tooltip_close_{};             // 关闭钮 20x20
    bool tooltip_arrow_down_ = true;   // true = 气泡在锚点上方，箭头朝下
    void OverlayDestroyed(const Control* control);
    static bool OverlayContains(const Control* overlay, const Control* node);
    void HideTooltip(bool immediate = false);
    float TickTooltip(double now_seconds); // 推进 dwell/淡入淡出，返回本帧透明度
    bool TooltipHit(Point p) const;        // 气泡 ∪ 箭头+间隙走廊
    bool TooltipHasPayload() const noexcept {
        return !tooltip_text_.empty() || tooltip_custom_ != nullptr;
    }
    void ClearTooltipHover();
    void SetTooltipHover(Control* hit, Point p, uint32_t buttons);
    std::function<bool()> closing_;

    bool layout_dirty_ = true;
    bool dirty_full_ = true;
    int dirty_count_ = 0;
    Rect dirty_rects_[kMaxDirtyRects]{};
    Rect paint_clip_{};
    bool closed_ = false;
    bool animating_ = false;
    bool painting_ = false;
    bool paint_again_ = false;
    bool in_size_move_ = false;
    bool tracking_mouse_ = false;
    bool ime_syncing_ = false;
    LARGE_INTEGER last_tick_{};
    std::vector<Control*> anim_targets_;
    std::unique_ptr<Dialog> owned_dialog_;
    std::shared_ptr<void> keep_alive_ = std::make_shared<char>();
    struct FrameCb {
        uint64_t id = 0;
        std::function<bool(float)> fn;
    };
    std::vector<FrameCb> frame_cbs_;
    uint64_t next_frame_id_ = 1;
    struct OleDropTarget;
    OleDropTarget* ole_drop_ = nullptr;
    Control* drop_armed_ = nullptr;
    void* uia_state_ = nullptr;

    friend class Window;   // Window 的成员函数需要读写内部状态
    friend struct OleDropTarget;
    friend struct UiaNode;
};

} // namespace lumen

#include "lumen/win_undef.h"
