#include "popup_window.h"
#include "app_host.h"
#include "native_callback.h"
#include "log.h"
#include "lumatext_bridge.h"
#include "renderer.h"
#include "text_service.h"
#include "window_impl.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <imm.h>

namespace lumen {
namespace {
constexpr float kPopupPad = 8.0f;
constexpr float kPopupGap = 6.0f;
constexpr float kScreenMargin = 8.0f;

bool VisibleTree(const Control* control) {
    for (const Control* node = control; node; node = node->Parent()) {
        if (!node->Visible()) return false;
    }
    return control != nullptr;
}
}

thread_local PopupWindow* PopupWindow::g_active = nullptr;

PopupWindow::PopupWindow(WindowImpl* impl) : impl_(impl), owner_(impl->Hwnd()) {}

PopupWindow::~PopupWindow() {
    Dismiss();
    DetachContent();
    DestroyPopup();
    if (entered_) Renderer::FlyoutLeave();
    if (g_active == this) g_active = nullptr;
    if (impl_) {
        impl_->keyboard_focus_ = keyboard_focus_return_;
        if (focus_return_ && focus_return_->window_ == impl_->api_ && VisibleTree(focus_return_.Get())) {
            if (GetFocus() == owner_) impl_->SetFocusControl(focus_return_.Get());
            else impl_->focus_restore_ = focus_return_.Get();
        }
    }
}

bool PopupWindow::Active() noexcept { return g_active != nullptr; }

void PopupWindow::RequestClose(WindowImpl* impl) {
    if (g_active && g_active->impl_ == impl) g_active->Dismiss();
}

void PopupWindow::Show(WindowImpl* impl, Control& content, const Control* anchor, float width) {
    NativeCallbackScope callback;
    // 借用内容的阻塞 API 不能安全排队；重入请求不能在旧会话栈上再开一个窗口。
    if (!impl || !impl->Hwnd() || Active() || content.Parent() || content.window_) return;
    PopupWindow popup(impl);
    g_active = &popup;
    popup.Run(content, anchor, width);
}

void PopupWindow::Layout() {
    layout_dirty_ = false;
    float max_width = requested_width_, max_height = 640.0f;
    POINT point{};
    if (anchor_) { point.x = static_cast<LONG>(anchor_->AbsoluteBounds().x * scale_); point.y = static_cast<LONG>(anchor_->AbsoluteBounds().y * scale_); }
    ClientToScreen(owner_, &point);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor)) {
        max_width = std::min(max_width, (monitor.rcWork.right - monitor.rcWork.left - 2.0f * kScreenMargin) / scale_);
        max_height = std::min(max_height, (monitor.rcWork.bottom - monitor.rcWork.top - 2.0f * kScreenMargin) / scale_);
    }
    max_width = std::max(32.0f, max_width);
    max_height = std::max(32.0f, max_height);
    Size desired = content_->Measure({max_width - kPopupPad * 2.0f, 1.0e5f}, *theme_);
    width_dip_ = std::clamp(desired.w + kPopupPad * 2.0f, std::min(120.0f, max_width), max_width);
    height_dip_ = std::clamp(desired.h + kPopupPad * 2.0f, kPopupPad * 2.0f + 1.0f, max_height);
    const bool scrolling = desired.h > height_dip_ - kPopupPad * 2.0f;
    const float content_width = std::max(1.0f, width_dip_ - kPopupPad * 2.0f - (scrolling ? 10.0f : 0.0f));
    desired = content_->Measure({content_width, 1.0e5f}, *theme_);
    content_height_ = std::max(desired.h, height_dip_ - kPopupPad * 2.0f);
    scroll_ = Clamp(scroll_, 0.0f, std::max(0.0f, content_height_ - height_dip_ + kPopupPad * 2.0f));
    content_->Arrange({kPopupPad, kPopupPad - scroll_, content_width, content_height_});
}

bool PopupWindow::ScrollBy(float delta) {
    if (!content_) return false;
    const float next = Clamp(scroll_ + delta, 0.0f, std::max(0.0f, content_height_ - height_dip_ + kPopupPad * 2.0f));
    if (next == scroll_) return false;
    const Rect bounds = content_->AbsoluteBounds();
    scroll_ = next;
    content_->Arrange({bounds.x, kPopupPad - scroll_, bounds.w, content_height_});
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    if (impl_) impl_->SyncImeCaret();
    return true;
}

void PopupWindow::EnsureFocusVisible() {
    if (!focus_) return;
    const Rect rect = focus_->AbsoluteBounds();
    if (rect.y < kPopupPad) ScrollBy(rect.y - kPopupPad);
    else if (rect.Bottom() > height_dip_ - kPopupPad) ScrollBy(rect.Bottom() - height_dip_ + kPopupPad);
}

void PopupWindow::Position() {
    POINT origin{0, 0};
    ClientToScreen(owner_, &origin);
    const Rect a = anchor_ ? anchor_->AbsoluteBounds() : Rect{};
    float px = a.x * scale_ + static_cast<float>(origin.x);
    float py = a.Bottom() * scale_ + static_cast<float>(origin.y) + kPopupGap * scale_;
    const HMONITOR mon = MonitorFromPoint({static_cast<LONG>(px), static_cast<LONG>(py)},
                                          MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(mon, &mi)) {
        const float w = width_dip_ * scale_;
        const float h = height_dip_ * scale_;
        const float left = static_cast<float>(mi.rcWork.left) + kScreenMargin;
        const float top = static_cast<float>(mi.rcWork.top) + kScreenMargin;
        const float right = static_cast<float>(mi.rcWork.right) - kScreenMargin;
        const float bottom = static_cast<float>(mi.rcWork.bottom) - kScreenMargin;
        if (py + h > bottom) {
            const float above = a.y * scale_ + static_cast<float>(origin.y) - kPopupGap * scale_ - h;
            if (above >= top) py = above;
        }
        px = std::clamp(px, left, std::max(left, right - w));
        py = std::clamp(py, top, std::max(top, bottom - h));
    }
    width_px_ = std::max(1, static_cast<int>(std::ceil(width_dip_ * scale_)));
    height_px_ = std::max(1, static_cast<int>(std::ceil(height_dip_ * scale_)));
    pos_ = {static_cast<LONG>(px), static_cast<LONG>(py)};
}

void PopupWindow::SyncPosition() {
    if (dismissed_ || positioning_ || !impl_) return;
    if (!content_ || !IsWindow(owner_) || !IsWindowVisible(owner_) || IsIconic(owner_) ||
        !VisibleTree(content_.Get()) || (has_anchor_ &&
        (!anchor_ || anchor_->window_ != impl_->api_ || !VisibleTree(anchor_.Get())))) {
        Dismiss();
        return;
    }
    positioning_ = true;
    const bool scale_changed = scale_ != impl_->Scale();
    scale_ = impl_->Scale();
    if (scale_changed || layout_dirty_) Layout();
    if (!dismissed_ && content_) {
        Position();
        if (hwnd_) {
            RECT current{};
            GetWindowRect(hwnd_, &current);
            if (current.left != pos_.x || current.top != pos_.y ||
                current.right - current.left != width_px_ || current.bottom - current.top != height_px_) {
                SetWindowPos(hwnd_, nullptr, pos_.x, pos_.y, width_px_, height_px_,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                if (renderer_) renderer_->Resize(width_px_, height_px_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }
    positioning_ = false;
}

void PopupWindow::Run(Control& content, const Control* anchor, float width) {
    content_ = &content;
    anchor_ = const_cast<Control*>(anchor);
    has_anchor_ = anchor != nullptr;
    theme_ = &impl_->ThemeRef();
    requested_width_ = std::isfinite(width) ? std::max(120.0f, width) : 260.0f;
    focus_return_ = impl_->focused_;
    keyboard_focus_return_ = impl_->keyboard_focus_;
    WindowImpl::BindWindowRecursive(&content, impl_->api_);
    impl_->SetFocusControl(nullptr);
    impl_->HideTooltip(true);
    SyncPosition();
    if (dismissed_ || !CreatePopup()) return;
    Renderer::FlyoutEnter();
    entered_ = true;
    // 不长期占用 capture：线程消息路由负责外点收起，主窗非客户区仍能拖动。
    MSG message{};
    while (!dismissed_) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        SyncPosition();
        if (message.message == WM_KEYDOWN && message.wParam != VK_ESCAPE && message.wParam != VK_TAB) {
            TranslateMessage(&message);
        }
        if (!dismissed_ && RouteMessage(message.hwnd, message.message, message.wParam, message.lParam)) {
            continue;
        }
        if (message.message != WM_KEYDOWN) TranslateMessage(&message);
        DispatchMessageW(&message);
        SyncPosition();
    }
}

bool PopupWindow::CreatePopup() {
    if (!EnsureLumenClass(LumenClass::Popup, &PopupWindow::WndProc, 0, true)) return false;
    DpiContextScope dpi_scope(owner_);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
        LumenClassName(LumenClass::Popup), L"", WS_POPUP, pos_.x, pos_.y, width_px_, height_px_,
        GetAncestor(owner_, GA_ROOT), nullptr, LumenModule(), this);
    if (!hwnd_) return false;
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->Init(hwnd_, width_px_, height_px_)) return false;
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void PopupWindow::DestroyPopup() {
    if (renderer_) {
        renderer_->Shutdown();
        renderer_.reset();
    }
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void PopupWindow::DetachContent() {
    if (detaching_) return;
    detaching_ = true;
    CancelPointer();
    SetHover(nullptr, {});
    SetFocus(nullptr);
    if (content_) {
        WindowImpl::ForgetTree(content_.Get());
        WindowImpl::BindWindowRecursive(content_.Get(), nullptr);
        content_.Reset();
    }
    detaching_ = false;
}

void PopupWindow::Dismiss() {
    if (dismissed_) return;
    dismissed_ = true;
    // 旧会话尚未退栈时也不能留下可见窗口或鼠标捕获。
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
        if (GetCapture() == hwnd_) ReleaseCapture();
        PostMessageW(hwnd_, WM_NULL, 0, 0);
    }
}

void PopupWindow::Paint() {
    if (!renderer_ || !content_ || dismissed_ || renderer_->DeferHostFrame()) return;
    if (renderer_->NeedsRecovery() && !renderer_->Recover()) return;
    ID2D1DeviceContext2* dc = renderer_->BeginDraw();
    if (!dc) return;
    dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    painter_.BeginFrame(dc, &UiText(), scale_);
    painter_.SetLumaText(renderer_->Luma());
    painter_.SetBackdrop(theme_->fill_input);
    const Rect surface{0.0f, 0.0f, width_dip_, height_dip_};
    DrawElevated(painter_, *theme_, surface, theme_->radius_flyout, Elevation::Overlay,
                 theme_->surface_flyout, false);
    const Rect viewport{kPopupPad, kPopupPad, width_dip_ - kPopupPad * 2.0f, height_dip_ - kPopupPad * 2.0f};
    painter_.PushClip(viewport);
    DrawControlTree(painter_, *theme_, content_.Get(), viewport);
    painter_.PopClip();
    if (content_height_ > viewport.h + 0.5f)
        painter_.DrawScrollThumb(MakeScrollThumb(viewport, content_height_, scroll_, 0.0f, true), theme_->scrollbar_thumb);
    painter_.EndFrame();
    if (!renderer_->EndDraw(true, nullptr, 0)) {
        if (App::HostMode()) renderer_->RequestHostFrame();
        else InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

Point PopupWindow::ToLocal(POINT screen_px) const {
    ScreenToClient(hwnd_, &screen_px);
    return {static_cast<float>(screen_px.x) / scale_, static_cast<float>(screen_px.y) / scale_};
}

void PopupWindow::SetHover(Control* hit, const Point& p) {
    if (hover_.Get() == hit) return;
    WeakRef<Control> next(hit);
    WeakRef<Control> old(hover_.Get());
    hover_.Reset();
    if (old) old->OnMouseLeave();
    if (next && !dismissed_) {
        hover_ = next.Get();
        hover_->mouse_local_ = WindowImpl::ToLocal(hover_.Get(), p);
        hover_->OnMouseEnter();
    }
}

void PopupWindow::SetFocus(Control* hit) {
    if (focus_.Get() == hit) return;
    WeakRef<Control> next(hit);
    WeakRef<Control> old(focus_.Get());
    focus_.Reset();
    if (old) {
        old->focused_ = false;
        old->OnFocusChanged(false);
        if (old) old->NotifyFocusEvent(false);
    }
    if (next && !dismissed_) {
        focus_ = next.Get();
        focus_->focused_ = true;
        focus_->OnFocusChanged(true);
        if (focus_) focus_->NotifyFocusEvent(true);
    }
    EnsureFocusVisible();
    if (impl_) impl_->SyncImeCaret();
}

Control* PopupWindow::Focused(WindowImpl* impl) {
    return g_active && g_active->impl_ == impl && !g_active->dismissed_ ? g_active->focus_.Get() : nullptr;
}

Point PopupWindow::OffsetInOwner(WindowImpl* impl) {
    if (!g_active || g_active->impl_ != impl || !g_active->hwnd_) return {};
    POINT point{};
    ClientToScreen(g_active->hwnd_, &point); ScreenToClient(g_active->owner_, &point);
    return {point.x / g_active->scale_, point.y / g_active->scale_};
}

bool PopupWindow::TrySetFocus(WindowImpl* impl, Control* control) {
    if (!g_active || g_active->impl_ != impl || g_active->dismissed_ || !control) return false;
    for (Control* node = control; node; node = node->Parent()) {
        if (node == g_active->content_.Get()) { g_active->SetFocus(control); return true; }
    }
    return false;
}

void PopupWindow::MoveFocus(bool backwards) {
    Control* first = nullptr;
    Control* last = nullptr;
    Control* previous = nullptr;
    Control* next = nullptr;
    bool seen = false;
    auto visit = [&](auto&& self, Control* node) -> void {
        if (!node || !node->Visible() || !node->Enabled()) return;
        if (node->Focusable()) {
            if (!first) first = node;
            if (node == focus_.Get()) { previous = last; seen = true; }
            else if (seen && !next) next = node;
            last = node;
        }
        if (auto* panel = node->AsPanel()) {
            for (size_t i = 0; i < panel->ChildCount(); ++i) self(self, &panel->Child(i));
        }
    };
    visit(visit, content_.Get());
    SetFocus(backwards ? (previous ? previous : last) : (next ? next : first));
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void PopupWindow::CancelPointer() {
    WeakRef<Control> old(captured_.Get());
    captured_.Reset();
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
    if (old) old->OnMouseUp({-1.0f, -1.0f}, 0);
}

void PopupWindow::RoutePointer(POINT screen_px, bool down, bool up, uint32_t buttons) {
    if (!content_ || !impl_ || dismissed_) return;
    const Point p = ToLocal(screen_px);
    const bool inside = Rect{0.0f, 0.0f, width_dip_, height_dip_}.Contains(p);
    if (down && !inside) { Dismiss(); return; }
    WeakRef<Control> hit(inside ? impl_->HitTree(content_.Get(), p, 2.0f) : nullptr);
    SetHover(hit.Get(), p);
    if (dismissed_ || !impl_) return;
    WeakRef<Control> target(captured_ ? captured_.Get() : hit.Get());
    if (!target) return;
    target->mouse_local_ = WindowImpl::ToLocal(target.Get(), p);
    if (down) {
        impl_->keyboard_focus_ = false;
        SetFocus(target->Focusable() ? target.Get() : nullptr);
        if (!target || dismissed_) return;
        captured_ = target.Get();
        SetCapture(hwnd_);
        if (target && !dismissed_) target->OnMouseDown(target->mouse_local_, buttons);
    } else if (up) {
        const bool was_captured = static_cast<bool>(captured_);
        captured_.Reset();
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (was_captured && target) target->OnMouseUp(target->mouse_local_, buttons);
    } else {
        target->OnMouseMove(target->mouse_local_, buttons);
    }
    if (hwnd_ && !dismissed_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool PopupWindow::RouteMessage(HWND source, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (dismissed_ || !hwnd_) return false;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (focus_ && focus_->ImeComposing() && (wparam == VK_ESCAPE || wparam == VK_RETURN || wparam == VK_TAB)) {
            if (HIMC context = ImmGetContext(owner_)) {
                ImmNotifyIME(context, NI_COMPOSITIONSTR, wparam == VK_ESCAPE ? CPS_CANCEL : CPS_COMPLETE, 0);
                ImmReleaseContext(owner_, context);
            }
            if (wparam == VK_ESCAPE && focus_) focus_->OnImeEnd();
            return true;
        }
        if (focus_ && focus_->OnKey(static_cast<uint32_t>(wparam))) return true;
        if (wparam == VK_ESCAPE) Dismiss();
        else if (msg == WM_SYSKEYDOWN) { Dismiss(); return false; }
        else {
            if (impl_) impl_->keyboard_focus_ = true;
            if (wparam == VK_TAB) MoveFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            else if (wparam == VK_NEXT) ScrollBy(height_dip_ - kPopupPad * 2.0f);
            else if (wparam == VK_PRIOR) ScrollBy(-height_dip_ + kPopupPad * 2.0f);
        }
        return true;
    }
    if (msg == WM_CHAR) {
        if (focus_ && !focus_->ImeComposing()) focus_->OnChar(static_cast<wchar_t>(wparam));
        return true;
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP) return true;
    if (msg == WM_NCLBUTTONDOWN || msg == WM_NCRBUTTONDOWN || msg == WM_NCMBUTTONDOWN) {
        if (source == owner_) return false;
        Dismiss();
        return true;
    }
    if (msg == WM_POINTERDOWN || msg == WM_POINTERUPDATE || msg == WM_POINTERUP) {
        POINTER_INFO info{};
        if (GetPointerInfo(GET_POINTERID_WPARAM(wparam), &info)) {
            pointer_time_ = GetMessageTime();
            RoutePointer(info.ptPixelLocation, msg == WM_POINTERDOWN, msg == WM_POINTERUP,
                         (info.pointerFlags & POINTER_FLAG_FIRSTBUTTON) ? MK_LBUTTON : 0);
        }
        return true;
    }
    if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        const Point p = ToLocal({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        WeakRef<Control> hit(impl_ && content_ ? impl_->HitTree(content_.Get(), p) : nullptr);
        const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                            static_cast<float>(WHEEL_DELTA);
        bool handled = false;
        for (WeakRef<Control> node(hit.Get()); node;) {
            WeakRef<Control> parent(node->Parent());
            if (msg == WM_MOUSEWHEEL ? node->OnWheel(delta) : node->OnHWheel(delta)) { handled = true; break; }
            node = parent.Get();
        }
        if (!handled && msg == WM_MOUSEWHEEL) ScrollBy(-delta * 48.0f);
        return true;
    }
    const bool down = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK ||
                      msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
    const bool up = msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP;
    if (msg != WM_MOUSEMOVE && !down && !up) return false;
    const ULONG extra = static_cast<ULONG>(GetMessageExtraInfo());
    if ((pointer_time_ != 0 && pointer_time_ == GetMessageTime()) ||
        (extra & 0xFFFFFF80u) == 0xFF515700u) return true;
    POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    if (source) ClientToScreen(source, &screen);
    RoutePointer(screen, down, up, static_cast<uint32_t>(wparam));
    return true;
}

bool PopupWindow::FilterInput(WindowImpl* impl, HWND source, UINT msg, WPARAM wparam, LPARAM lparam) {
    return g_active && g_active->impl_ == impl && g_active->RouteMessage(source, msg, wparam, lparam);
}

void PopupWindow::OwnerMessage(WindowImpl* impl, UINT msg, WPARAM wparam) {
    if (!g_active || g_active->impl_ != impl) return;
    if ((msg == WM_SHOWWINDOW && !wparam) || (msg == WM_SIZE && wparam == SIZE_MINIMIZED) ||
        (msg == WM_ACTIVATEAPP && !wparam) || msg == WM_CANCELMODE) {
        g_active->Dismiss();
    } else if (msg == WM_WINDOWPOSCHANGED || msg == WM_DPICHANGED) {
        g_active->SyncPosition();
    }
}

void PopupWindow::OwnerDestroyed(WindowImpl* impl) {
    if (!g_active || g_active->impl_ != impl) return;
    auto* popup = g_active;
    popup->Dismiss();
    popup->DetachContent();
    popup->impl_ = nullptr;
    impl->popup_open_ = false;
}

void PopupWindow::OwnerInvalidated(WindowImpl* impl, bool layout) {
    if (!g_active || g_active->impl_ != impl || g_active->dismissed_) return;
    auto* popup = g_active;
    if (layout) popup->layout_dirty_ = true;
    if (popup->hwnd_) InvalidateRect(popup->hwnd_, nullptr, FALSE);
}

void PopupWindow::ForgetControl(WindowImpl* impl, const Control* control) {
    if (!g_active || g_active->impl_ != impl || g_active->detaching_) return;
    auto* popup = g_active;
    if (popup->anchor_.Get() == control || popup->content_.Get() == control) popup->Dismiss();
    if (popup->hover_.Get() == control) popup->hover_.Reset();
    if (popup->captured_.Get() == control) popup->captured_.Reset();
    if (popup->focus_.Get() == control) popup->focus_.Reset();
    if (popup->focus_return_.Get() == control) popup->focus_return_.Reset();
}

LRESULT CALLBACK PopupWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    NativeCallbackScope callback;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<PopupWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Handle(hwnd, msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT PopupWindow::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (RouteMessage(hwnd, msg, wparam, lparam)) return 0;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        Paint();
        return 0;
    }
    case WM_TIMER:
        if (renderer_ && renderer_->HandleFrameTimer(static_cast<UINT_PTR>(wparam))) return 0;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_CAPTURECHANGED:
        if (captured_ && reinterpret_cast<HWND>(lparam) != hwnd) Dismiss();
        return 0;
    case WM_CANCELMODE:
    case WM_CLOSE:
        Dismiss();
        return 0;
    case WM_ACTIVATEAPP:
        if (!wparam) Dismiss();
        return 0;
    case WM_DPICHANGED:
        layout_dirty_ = true;
        SyncPosition();
        return 0;
    case WM_NCDESTROY:
        Dismiss();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    default: return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void WindowImpl::ShowPopup(Control& content, const Control* anchor, float width,
                           std::function<void()> closed) {
    if (!hwnd_ || PopupWindow::Active() || content.Parent() || content.window_) return;
    auto port = port_;
    popup_open_ = true;
    try {
        PopupWindow::Show(this, content, anchor, width);
    } catch (...) {
        if (port->target.load(std::memory_order_acquire)) popup_open_ = false;
        throw;
    }
    // 回调或消息可能已销毁 owner，不能在阻塞会话返回后再访问旧 this。
    if (!port->target.load(std::memory_order_acquire)) return;
    popup_open_ = false;
    Invalidate();
    if (closed) closed();
}

void WindowImpl::ClosePopup() { PopupWindow::RequestClose(this); }

} // namespace lumen
