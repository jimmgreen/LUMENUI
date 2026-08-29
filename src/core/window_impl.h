// window_impl.h — Window 的内部实现：Win32 窗口、渲染、输入、焦点、动画。
#pragma once
#include "fluentui/Window.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "renderer.h"
#include "text_service.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace fui {

class Dialog;
class MenuWindow;

class WindowImpl {
public:
    WindowImpl(Window* api, std::wstring_view title, Size client_size);
    ~WindowImpl();

    // 控件基类经 Window API 转发的内部通道。
    static void Invalidate(Window* window) { window->Impl()->Invalidate(); }
    static void Animate(Window* window) { window->Impl()->RequestAnimation(); }
    static void Relayout(Window* window) { window->Impl()->RequestRelayout(); }
    static void SetFocusTo(Window* window, Control* control) {
        window->Impl()->SetFocusControl(control);
    }
    static HWND HwndOf(Window* window) { return window->Impl()->Hwnd(); }
    static float ScaleOf(Window* window) { return window->Impl()->Scale(); }
    static Theme& ThemeOf(Window* window) { return window->Impl()->ThemeRef(); }

    void Show();
    void Close();
    bool Closed() const noexcept { return closed_; }
    void Title(std::wstring_view text);
    void Resize(Size client_size);
    void MinSize(Size min_size);
    void Invalidate();
    void RequestAnimation();
    void RequestRelayout();
    void SetFocusControl(Control* control);
    void SetThemeMode(ThemeMode mode);
    void ShowDialog(Dialog& dialog);
    void CloseDialog();
    void* NativeHandle() const noexcept { return hwnd_; }
    StackPanel& Root() noexcept { return *root_; }
    Theme& ThemeRef() noexcept { return theme_; }
    float Scale() const noexcept { return scale_; }
    HWND Hwnd() const noexcept { return hwnd_; }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void Paint();
    void Layout();
    void DrawTree(Control* control);
    void UpdateClientSize();
    void RefreshTheme();
    void TickAnimations();
    void CollectFocusable(Control* tree, std::vector<Control*>& order);
    static Point ToLocal(const Control* control, Point absolute);
    Control* HitTest(Point p);
    Control* HitTree(Control* control, Point p);
    void OnMouseMove(int px, int py, uint32_t buttons);
    void OnMouseButton(int px, int py, uint32_t buttons, bool down);
    bool OnKeyDown(uint32_t vk);
    void TrackMouse();
    static void EnsureWindowClass();
    Control* FindFirstFocusable();
    Control* FindNextFocusable(Control* current);

    Window* api_;
    HWND hwnd_ = nullptr;
    float scale_ = 1.0f;
    int client_w_ = 0, client_h_ = 0;   // 物理像素
    Size min_size_dip_{0.0f, 0.0f};

    Renderer renderer_;
    Painter painter_;
    Theme theme_;
    ThemeMode theme_mode_ = ThemeMode::System;

    std::unique_ptr<StackPanel> root_;
    Control* hovered_ = nullptr;
    Control* captured_ = nullptr;
    Control* focused_ = nullptr;
    Dialog* active_dialog_ = nullptr;
    std::function<bool()> closing_;

    bool layout_dirty_ = true;
    bool closed_ = false;
    bool anim_timer_ = false;
    bool tracking_mouse_ = false;
    LARGE_INTEGER last_tick_{};

    friend class Window;   // Window 的成员函数需要读写内部状态
};

} // namespace fui
