// popup_window.h — 借用控件子树的独立弹窗；每个 UI 线程仅允许一个阻塞会话。
#pragma once
#include "lumen/Control.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <memory>

namespace lumen {

class Renderer;
class WindowImpl;

class PopupWindow {
public:
    static bool Active() noexcept;
    static void Show(WindowImpl* impl, Control& content, const Control* anchor, float width);
    static void RequestClose(WindowImpl* impl);
    static bool FilterInput(WindowImpl* impl, HWND source, UINT msg, WPARAM wparam, LPARAM lparam);
    static void OwnerMessage(WindowImpl* impl, UINT msg, WPARAM wparam);
    static void OwnerDestroyed(WindowImpl* impl);
    static void OwnerInvalidated(WindowImpl* impl, bool layout = false);
    static void ForgetControl(WindowImpl* impl, const Control* control);
    static Control* Focused(WindowImpl* impl);
    static Point OffsetInOwner(WindowImpl* impl);
    static bool TrySetFocus(WindowImpl* impl, Control* control);

private:
    explicit PopupWindow(WindowImpl* impl);
    ~PopupWindow();
    void Run(Control& content, const Control* anchor, float width);
    bool CreatePopup();
    void DestroyPopup();
    void DetachContent();
    void Layout();
    void Position();
    void SyncPosition();
    void Paint();
    bool RouteMessage(HWND source, UINT msg, WPARAM wparam, LPARAM lparam);
    void RoutePointer(POINT screen_px, bool down, bool up, uint32_t buttons);
    void SetHover(Control* hit, const Point& p);
    void SetFocus(Control* hit);
    void MoveFocus(bool backwards);
    bool ScrollBy(float delta);
    void EnsureFocusVisible();
    void CancelPointer();
    Point ToLocal(POINT screen_px) const;
    void Dismiss();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    WindowImpl* impl_ = nullptr;
    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    Painter painter_;
    WeakRef<Control> content_;
    WeakRef<Control> anchor_;
    WeakRef<Control> hover_;
    WeakRef<Control> captured_;
    WeakRef<Control> focus_;
    WeakRef<Control> focus_return_;
    const Theme* theme_ = nullptr;
    float scale_ = 1.0f;
    float requested_width_ = 260.0f;
    int width_px_ = 0, height_px_ = 0;
    float width_dip_ = 0.0f, height_dip_ = 0.0f;
    float content_height_ = 0.0f, scroll_ = 0.0f;
    POINT pos_{0, 0};
    bool has_anchor_ = false;
    bool dismissed_ = false;
    bool entered_ = false;
    bool layout_dirty_ = true;
    bool positioning_ = false;
    bool detaching_ = false;
    bool keyboard_focus_return_ = false;
    LONG pointer_time_ = 0;
    static thread_local PopupWindow* g_active;
};

} // namespace lumen
