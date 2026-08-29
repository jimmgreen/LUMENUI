#include "window_impl.h"
#include "fluentui/Dialog.h"
#include "fluentui/TextBox.h"
#include <windowsx.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <vector>

namespace fui {
namespace {
constexpr UINT kAnimTimerId = 1;
constexpr UINT kAnimIntervalMs = 16;
} // namespace

Window::Window(std::wstring_view title, Size client_size)
    : impl_(std::make_unique<WindowImpl>(this, title, client_size)) {}

Window::~Window() = default;

WindowImpl* Window::Impl() const noexcept { return impl_.get(); }
StackPanel& Window::Root() { return impl_->Root(); }
void Window::Show() { impl_->Show(); }
void Window::Close() { impl_->Close(); }
bool Window::Closed() const { return impl_->Closed(); }
void Window::Title(std::wstring_view text) { impl_->Title(text); }
void Window::Resize(Size client_size) { impl_->Resize(client_size); }
void Window::MinSize(Size min_size) { impl_->MinSize(min_size); }
ThemeMode Window::Theme() const { return impl_->theme_mode_; }
void Window::SetTheme(ThemeMode mode) { impl_->SetThemeMode(mode); }
void Window::OnClosing(std::function<bool()> callback) { impl_->closing_ = std::move(callback); }
void Window::ShowDialog(Dialog& dialog) { impl_->ShowDialog(dialog); }
void Window::CloseDialog() { impl_->CloseDialog(); }
bool Window::DialogActive() const { return impl_->active_dialog_ != nullptr; }
void Window::Invalidate() { impl_->Invalidate(); }
void* Window::NativeHandle() const { return impl_->NativeHandle(); }

WindowImpl::WindowImpl(Window* api, std::wstring_view title, Size client_size) : api_(api) {
    root_ = std::make_unique<StackPanel>();
    root_->window_ = api_;
    EnsureWindowClass();
    scale_ = static_cast<float>(GetDpiForSystem()) / 96.0f;
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        UINT dpi_x = 96, dpi_y = 96;
        if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
            scale_ = static_cast<float>(dpi_x) / 96.0f;
        }
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD ex_style = WS_EX_NOREDIRECTIONBITMAP;
    RECT rect{0, 0, static_cast<LONG>(client_size.w * scale_),
              static_cast<LONG>(client_size.h * scale_)};
    AdjustWindowRectExForDpi(&rect, style, FALSE, ex_style, static_cast<UINT>(scale_ * 96.0f));
    hwnd_ = CreateWindowExW(ex_style, L"fui_window", std::wstring(title).c_str(), style,
                            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                            rect.bottom - rect.top, nullptr, nullptr, GetModuleHandleW(nullptr),
                            this);
    UpdateClientSize();
    RefreshTheme();
    renderer_.Init(hwnd_, client_w_, client_h_);
}

WindowImpl::~WindowImpl() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void WindowImpl::EnsureWindowClass() {
    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &WindowImpl::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"fui_window";
        return RegisterClassExW(&wc) != 0;
    }();
    (void)registered;
}

LRESULT CALLBACK WindowImpl::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    return self->Handle(hwnd, msg, wparam, lparam);
}

LRESULT WindowImpl::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!hwnd_) hwnd_ = hwnd;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        EndPaint(hwnd_, &ps);
        Paint();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wparam == kAnimTimerId) TickAnimations();
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            UpdateClientSize();
            renderer_.Resize(client_w_, client_h_);
            RequestRelayout();
            Invalidate();
        }
        return 0;
    case WM_DPICHANGED: {
        scale_ = static_cast<float>(HIWORD(wparam)) / 96.0f;
        auto* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested) {
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        UpdateClientSize();
        RequestRelayout();
        Invalidate();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        if (min_size_dip_.w > 0.0f && min_size_dip_.h > 0.0f) {
            RECT rect{0, 0, static_cast<LONG>(min_size_dip_.w * scale_),
                      static_cast<LONG>(min_size_dip_.h * scale_)};
            AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_NOREDIRECTIONBITMAP,
                                     static_cast<UINT>(scale_ * 96.0f));
            info->ptMinTrackSize.x = rect.right - rect.left;
            info->ptMinTrackSize.y = rect.bottom - rect.top;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam));
        return 0;
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        if (hovered_) {
            hovered_->OnMouseLeave();
            hovered_ = nullptr;
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      true);
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        OnMouseButton(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), static_cast<uint32_t>(wparam),
                      false);
        return 0;
    case WM_LBUTTONDBLCLK: {
        Point p{static_cast<float>(GET_X_LPARAM(lparam)) / scale_,
                static_cast<float>(GET_Y_LPARAM(lparam)) / scale_};
        if (Control* hit = HitTest(p)) hit->OnMouseDoubleClick(WindowImpl::ToLocal(hit, p));
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &screen);
        Point p{static_cast<float>(screen.x) / scale_, static_cast<float>(screen.y) / scale_};
        if (Control* hit = HitTest(p)) {
            hit->OnWheel(static_cast<short>(HIWORD(wparam)) / float(WHEEL_DELTA));
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        captured_ = nullptr;
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (OnKeyDown(static_cast<uint32_t>(wparam))) return 0;
        break;
    case WM_CHAR:
        if (focused_) focused_->OnChar(static_cast<wchar_t>(wparam));
        return 0;
    case WM_SETTINGCHANGE:
        if (lparam && lstrcmpiW(reinterpret_cast<LPCWSTR>(lparam), L"ImmersiveColorSet") == 0) {
            RefreshTheme();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            if (dynamic_cast<TextBox*>(hovered_)) {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return TRUE;
        }
        break;
    case WM_CLOSE:
        if (closing_ && !closing_()) return 0;
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        closed_ = true;
        KillTimer(hwnd_, kAnimTimerId);
        anim_timer_ = false;
        hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WindowImpl::UpdateClientSize() {
    RECT client{};
    if (GetClientRect(hwnd_, &client)) {
        client_w_ = client.right - client.left;
        client_h_ = client.bottom - client.top;
    }
}

void WindowImpl::RefreshTheme() {
    const bool dark = theme_mode_ == ThemeMode::Dark ||
                      (theme_mode_ == ThemeMode::System && SystemPrefersDark());
    theme_ = MakeTheme(dark, SystemAccentColor());
    Invalidate();
}

void WindowImpl::Show() {
    ShowWindow(hwnd_, SW_SHOW);
    Invalidate();
}

void WindowImpl::Close() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void WindowImpl::Title(std::wstring_view text) {
    if (hwnd_) SetWindowTextW(hwnd_, std::wstring(text).c_str());
}

void WindowImpl::Resize(Size client_size) {
    RECT rect{0, 0, static_cast<LONG>(client_size.w * scale_),
              static_cast<LONG>(client_size.h * scale_)};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_NOREDIRECTIONBITMAP,
                             static_cast<UINT>(scale_ * 96.0f));
    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WindowImpl::MinSize(Size min_size) {
    min_size_dip_ = min_size;
}

void WindowImpl::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void WindowImpl::RequestRelayout() {
    layout_dirty_ = true;
}

void WindowImpl::RequestAnimation() {
    if (anim_timer_ || !hwnd_) return;
    QueryPerformanceCounter(&last_tick_);
    SetTimer(hwnd_, kAnimTimerId, kAnimIntervalMs, nullptr);
    anim_timer_ = true;
}

void WindowImpl::SetFocusControl(Control* control) {
    if (focused_ == control) return;
    if (focused_) {
        focused_->focused_ = false;
        focused_->OnFocusChanged(false);
    }
    focused_ = control;
    if (focused_) {
        focused_->focused_ = true;
        focused_->OnFocusChanged(true);
    }
    Invalidate();
}

void WindowImpl::SetThemeMode(ThemeMode mode) {
    if (theme_mode_ == mode) return;
    theme_mode_ = mode;
    RefreshTheme();
}

void WindowImpl::ShowDialog(Dialog& dialog) {
    active_dialog_ = &dialog;
    dialog.parent_ = nullptr;
    dialog.window_ = api_;
    layout_dirty_ = true;
    if (Control* first = FindFirstFocusable()) SetFocusControl(first);
    Invalidate();
}

void WindowImpl::CloseDialog() {
    if (!active_dialog_) return;
    active_dialog_ = nullptr;
    layout_dirty_ = true;
    Invalidate();
}

void WindowImpl::Layout() {
    layout_dirty_ = false;
    const float w = client_w_ / scale_;
    const float h = client_h_ / scale_;
    Control& root_control = *root_;
    root_control.Measure({w, h}, theme_);
    root_control.Arrange({0.0f, 0.0f, w, h});
    if (active_dialog_) {
        active_dialog_->Measure({w, h}, theme_);
        const float dialog_w = std::min(420.0f, w - 24.0f);
        const float dialog_h = std::max(active_dialog_->desired_.h, 120.0f);
        active_dialog_->Arrange({(w - dialog_w) * 0.5f, (h - dialog_h) * 0.5f, dialog_w, dialog_h});
    }
}

void WindowImpl::DrawTree(Control* control) {
    if (!control || !control->visible_) return;
    control->Draw(painter_, theme_);
    if (auto* panel = dynamic_cast<Panel*>(control)) {
        for (size_t i = 0; i < panel->ChildCount(); ++i) DrawTree(&panel->Child(i));
    }
}

void WindowImpl::Paint() {
    if (!hwnd_ || client_w_ <= 0 || client_h_ <= 0) return;
    if (!renderer_.Ready() && !renderer_.Recover()) return;
    if (renderer_.NeedsRecovery()) {
        if (!renderer_.Recover()) return;
        layout_dirty_ = true;
    }
    if (layout_dirty_) Layout();
    ID2D1DeviceContext2* dc = renderer_.BeginDraw();
    if (!dc) return;
    painter_.BeginFrame(dc, &UiText(), scale_);
    const Rect client{0.0f, 0.0f, client_w_ / scale_, client_h_ / scale_};
    painter_.FillRect(client, theme_.bg);
    DrawTree(root_.get());
    if (active_dialog_) {
        painter_.FillRect(client, Color{0.0f, 0.0f, 0.0f, 0.35f});
        DrawTree(active_dialog_);
    }
    painter_.EndFrame();
    if (!renderer_.EndDraw()) Invalidate();
}

void WindowImpl::TickAnimations() {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = static_cast<float>(now.QuadPart - last_tick_.QuadPart) /
               static_cast<float>(frequency.QuadPart);
    last_tick_ = now;
    dt = std::clamp(dt, 0.0f, 0.1f);

    bool more = false;
    Control* trees[2] = {root_.get(), active_dialog_};
    for (Control* tree : trees) {
        if (!tree) continue;
        std::vector<Control*> stack{tree};
        while (!stack.empty()) {
            Control* current = stack.back();
            stack.pop_back();
            if (!current || !current->visible_) continue;
            if (current->OnAnimate(dt)) more = true;
            if (auto* panel = dynamic_cast<Panel*>(current)) {
                for (size_t i = 0; i < panel->ChildCount(); ++i) stack.push_back(&panel->Child(i));
            }
        }
    }
    if (more) {
        Invalidate();
    } else {
        KillTimer(hwnd_, kAnimTimerId);
        anim_timer_ = false;
    }
}

Control* WindowImpl::HitTree(Control* control, Point p) {
    if (!control || !control->visible_ || !control->enabled_) return nullptr;
    if (!control->absolute_.Contains(p)) return nullptr;
    if (auto* panel = dynamic_cast<Panel*>(control)) {
        for (size_t i = panel->ChildCount(); i-- > 0;) {
            if (Control* hit = HitTree(&panel->Child(i), p)) return hit;
        }
    }
    return control->HitTransparent() ? nullptr : control;
}

Control* WindowImpl::HitTest(Point p) {
    if (active_dialog_) return HitTree(active_dialog_, p);
    return HitTree(root_.get(), p);
}

void WindowImpl::TrackMouse() {
    if (tracking_mouse_) return;
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&track);
    tracking_mouse_ = true;
}

void WindowImpl::OnMouseMove(int px, int py, uint32_t buttons) {
    Point p{static_cast<float>(px) / scale_, static_cast<float>(py) / scale_};
    if (!captured_) {
        Control* hit = HitTest(p);
        if (hit != hovered_) {
            if (hovered_) hovered_->OnMouseLeave();
            hovered_ = hit;
            if (hovered_) hovered_->OnMouseEnter();
        }
        TrackMouse();
    }
    Control* target = captured_ ? captured_ : hovered_;
    if (target) target->OnMouseMove(WindowImpl::ToLocal(target, p), buttons);
}

void WindowImpl::OnMouseButton(int px, int py, uint32_t buttons, bool down) {
    Point p{static_cast<float>(px) / scale_, static_cast<float>(py) / scale_};
    if (down) {
        Control* hit = HitTest(p);
        if (!hit && active_dialog_) {
            // 模态下点击遮罩：默认关闭型对话框直接关闭。
            if (active_dialog_->default_close_) CloseDialog();
            return;
        }
        if (focused_ != hit) SetFocusControl(hit && hit->Focusable() ? hit : nullptr);
        if (hit) {
            captured_ = hit;
            SetCapture(hwnd_);
            hit->OnMouseDown(WindowImpl::ToLocal(hit, p), buttons);
        }
    } else {
        if (captured_) {
            Control* target = captured_;
            captured_ = nullptr;
            ReleaseCapture();
            target->OnMouseUp(WindowImpl::ToLocal(target, p), buttons);
        }
    }
}

bool WindowImpl::OnKeyDown(uint32_t vk) {
    if (vk == VK_ESCAPE && active_dialog_) {
        active_dialog_->OnKey(vk);
        return true;
    }
    if (focused_ && focused_->OnKey(vk)) return true;
    if (vk == VK_TAB) {
        Control* next = focused_ ? FindNextFocusable(focused_) : FindFirstFocusable();
        if (next) SetFocusControl(next);
        return true;
    }
    return false;
}

void WindowImpl::CollectFocusable(Control* tree, std::vector<Control*>& order) {
    std::vector<Control*> stack{tree};
    while (!stack.empty()) {
        Control* node = stack.back();
        stack.pop_back();
        if (!node || !node->visible_ || !node->enabled_) continue;
        if (node->Focusable()) order.push_back(node);
        if (auto* panel = dynamic_cast<Panel*>(node)) {
            for (size_t i = panel->ChildCount(); i-- > 0;) stack.push_back(&panel->Child(i));
        }
    }
}

Control* WindowImpl::FindFirstFocusable() {
    std::vector<Control*> order;
    if (active_dialog_) CollectFocusable(active_dialog_, order);
    else CollectFocusable(root_.get(), order);
    return order.empty() ? nullptr : order[0];
}

Control* WindowImpl::FindNextFocusable(Control* current) {
    std::vector<Control*> order;
    if (active_dialog_) CollectFocusable(active_dialog_, order);
    else CollectFocusable(root_.get(), order);
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == current) return order[(i + 1) % order.size()];
    }
    return order.empty() ? nullptr : order[0];
}

} // namespace fui

namespace fui {
Point WindowImpl::ToLocal(const Control* control, Point absolute) {
    return {absolute.x - control->absolute_.x, absolute.y - control->absolute_.y};
}
} // namespace fui
