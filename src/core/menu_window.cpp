#include "menu_window.h"
#include "lumen/Animate.h"
#include "lumen/Command.h"
#include "lumen/Icons.h"
#include "lumen/Menu.h"
#include "lumen/Painter.h"
#include "log.h"
#include "lumatext_bridge.h"
#include "renderer.h"
#include "text_service.h"
#include "window_impl.h"
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace lumen {
namespace {

constexpr float kMenuPad = 4.0f;
constexpr float kItemPadX = 16.0f;
constexpr float kGutter = 32.0f;
constexpr float kItemRightPad = 12.0f;
constexpr float kHeaderHeight = 28.0f;
constexpr float kAppearSeconds = 0.16f;
constexpr UINT_PTR kTimerOpen = 1;
constexpr UINT_PTR kTimerClose = 2;
constexpr UINT kOpenDelayMs = 250;
constexpr UINT kCloseDelayMs = 200;

Color Fade(Color c, float a) {
    c.a *= a;
    return c;
}

bool PointInTriangle(POINT p, POINT a, POINT b, POINT c) noexcept {
    const auto sign = [](POINT p1, POINT p2, POINT p3) noexcept {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    const LONG s1 = sign(p, a, b);
    const LONG s2 = sign(p, b, c);
    const LONG s3 = sign(p, c, a);
    const bool neg = s1 < 0 || s2 < 0 || s3 < 0;
    const bool pos = s1 > 0 || s2 > 0 || s3 > 0;
    return !(neg && pos);
}

void ApplyRadio(std::vector<MenuItem>& items, size_t picked) {
    const std::wstring& group = items[picked].radio_group;
    for (MenuItem& it : items) {
        if (!it.radio) continue;
        if (group.empty() || it.radio_group == group) it.checked = false;
    }
    items[picked].checked = true;
}

void DrawAccessLabel(Painter& painter, std::wstring_view raw, const Rect& r, TextRole role,
                     Color color, Align align, LumaTextBridge* luma) {
    std::wstring display;
    int index = -1;
    wchar_t key = 0;
    MenuParseAccess(raw, display, index, key);
    (void)key;
    painter.DrawText(display, r, role, color, align);
    DrawMnemonicUnderline(painter, display, index, r, role, color, align, luma);
}

} // namespace

MenuWindow::MenuWindow(std::vector<MenuItem>& items, const Theme& theme, float scale,
                       float min_width_dip)
    : items_(items), theme_(theme), scale_(scale), min_width_dip_(min_width_dip) {}

MenuWindow::~MenuWindow() { CloseSubmenu(); }

void MenuWindow::ComputeLayout() {
    row_item_.clear();
    float max_text_w = 0.0f, max_shortcut_w = 0.0f;
    bool any_shortcut = false;
    any_gutter_ = false;
    LumaTextBridge* luma = renderer_ ? renderer_->Luma() : nullptr;
    for (const MenuItem& item : items_) {
        if (item.separator) continue;
        if (!item.glyph.empty() || item.checked || item.checkable || item.radio) any_gutter_ = true;
        const std::wstring label = MenuLabel(item.text);
        max_text_w = std::max(max_text_w, MeasureUiText(label, TextRole::Body, 0.0f, luma).w);
        if (!item.shortcut.empty()) {
            any_shortcut = true;
            max_shortcut_w = std::max(max_shortcut_w,
                                      MeasureUiText(item.shortcut, TextRole::Caption, 0.0f, luma).w);
        }
    }
    const float text_x = any_gutter_ ? kGutter : kItemPadX;
    width_dip_ = text_x + max_text_w + kItemRightPad * 2.0f +
                 (any_shortcut ? max_shortcut_w + 16.0f : 0.0f);
    width_dip_ = std::max(width_dip_, 180.0f);
    width_dip_ = std::max(width_dip_, min_width_dip_);
    height_dip_ = kMenuPad;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].separator) {
            row_item_.push_back(-1);
            height_dip_ += 9.0f;
        } else {
            row_item_.push_back(static_cast<int>(i));
            height_dip_ += items_[i].header ? kHeaderHeight : theme_.menu_item_height;
        }
    }
    height_dip_ += kMenuPad;
    content_height_dip_ = height_dip_;
    scroll_y_ = 0.0f;
    max_scroll_y_ = 0.0f;
}

float MenuWindow::RowHeightAt(size_t row) const noexcept {
    if (row >= row_item_.size()) return 0.0f;
    const int item = row_item_[row];
    if (item < 0) return 9.0f;
    return items_[static_cast<size_t>(item)].header ? kHeaderHeight : theme_.menu_item_height;
}

bool MenuWindow::RowInteractive(size_t row) const noexcept {
    if (row >= row_item_.size()) return false;
    const int item = row_item_[row];
    if (item < 0) return false;
    const MenuItem& it = items_[static_cast<size_t>(item)];
    return !it.disabled && !it.header && !it.separator;
}

int MenuWindow::RowAt(float x_dip, float y_dip) const {
    if (x_dip < 0.0f || y_dip < 0.0f || x_dip > width_dip_ || y_dip > height_dip_) return -1;
    y_dip += scroll_y_;
    float cursor = kMenuPad;
    for (size_t i = 0; i < row_item_.size(); ++i) {
        const float row_h = RowHeightAt(i);
        if (y_dip >= cursor && y_dip < cursor + row_h) {
            return RowInteractive(i) ? static_cast<int>(i) : -1;
        }
        cursor += row_h;
    }
    return -1;
}

bool MenuWindow::ContainsScreenPx(POINT screen_px) const {
    if (!hwnd_) return false;
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    return PtInRect(&wr, screen_px) != FALSE;
}

int MenuWindow::Show(HWND owner, std::vector<MenuItem>& items, POINT screen_px,
                     const Theme& theme, float scale, float min_width_dip,
                     std::function<bool(wchar_t)> on_char) {
    MenuWindow menu(items, theme, scale, min_width_dip);
    menu.on_char_ = std::move(on_char);
    return menu.Run(owner, screen_px);
}

void MenuWindow::Reload() {
    ComputeLayout();
    if (!hwnd_) return;
    width_px_ = static_cast<int>(width_dip_ * scale_);
    height_px_ = static_cast<int>(height_dip_ * scale_);
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    POINT origin{wr.left, wr.top};
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(mon, &mi)) {
        const int max_h = (std::max)(120, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top - 24));
        if (height_px_ > max_h) {
            height_px_ = max_h;
            height_dip_ = static_cast<float>(height_px_) / scale_;
            max_scroll_y_ = (std::max)(0.0f, content_height_dip_ - height_dip_);
        }
        origin.x = std::clamp(origin.x, mi.rcWork.left,
                              (std::max)(mi.rcWork.left, mi.rcWork.right - width_px_));
        if (origin.y + height_px_ > mi.rcWork.bottom) {
            origin.y = (std::max)(mi.rcWork.top, mi.rcWork.bottom - height_px_);
        }
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, origin.x, origin.y, width_px_, height_px_,
                 SWP_NOACTIVATE);
    if (renderer_) renderer_->Resize(width_px_, height_px_);
    if (hover_row_ >= static_cast<int>(row_item_.size())) hover_row_ = -1;
    ClampScroll();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool MenuWindow::CreatePopup(HWND owner, POINT screen_px) {
    ComputeLayout();
    if (row_item_.empty()) {
        Log(L"menu CreatePopup abort empty rows");
        return false;
    }

    width_px_ = static_cast<int>(width_dip_ * scale_);
    height_px_ = static_cast<int>(height_dip_ * scale_);
    HMONITOR mon = MonitorFromPoint(screen_px, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(mon, &mi)) {
        const int max_h = (std::max)(120, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top - 24));
        if (height_px_ > max_h) {
            height_px_ = max_h;
            height_dip_ = static_cast<float>(height_px_) / scale_;
            max_scroll_y_ = (std::max)(0.0f, content_height_dip_ - height_dip_);
        }
        screen_px.x = std::clamp(screen_px.x, mi.rcWork.left,
                                 (std::max)(mi.rcWork.left, mi.rcWork.right - width_px_));
        if (screen_px.y + height_px_ > mi.rcWork.bottom) {
            screen_px.y = (std::max)(mi.rcWork.top, mi.rcWork.bottom - height_px_);
        }
        if (parent_ && parent_->hwnd_) {
            RECT parent_wr{};
            GetWindowRect(parent_->hwnd_, &parent_wr);
            if (screen_px.x + width_px_ > mi.rcWork.right &&
                parent_wr.left - width_px_ + 4 >= mi.rcWork.left) {
                screen_px.x = parent_wr.left - width_px_ + 4;
            }
        }
    }

    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MenuWindow::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"lumen_menu";
        return RegisterClassExW(&wc) != 0;
    }();
    (void)registered;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST,
        L"lumen_menu", L"", WS_POPUP, screen_px.x, screen_px.y, width_px_, height_px_, owner,
        nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) {
        Log(L"menu CreateWindow fail lastError=%lu pos=(%ld,%ld) size=(%d,%d)", GetLastError(),
            screen_px.x, screen_px.y, width_px_, height_px_);
        return false;
    }
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->Init(hwnd_, width_px_, height_px_)) {
        Log(L"menu Renderer::Init fail hwnd=%p size=(%d,%d)", hwnd_, width_px_, height_px_);
        renderer_->Shutdown();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    // Init 后才有 Luma，按同一套测量重排，避免快捷键列宽和绘制不一致。
    ComputeLayout();
    width_px_ = static_cast<int>(width_dip_ * scale_);
    height_px_ = static_cast<int>(height_dip_ * scale_);
    if (GetMonitorInfoW(mon, &mi)) {
        const int max_h = (std::max)(120, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top - 24));
        if (height_px_ > max_h) {
            height_px_ = max_h;
            height_dip_ = static_cast<float>(height_px_) / scale_;
            max_scroll_y_ = (std::max)(0.0f, content_height_dip_ - height_dip_);
        }
        screen_px.x = std::clamp(screen_px.x, mi.rcWork.left,
                                 (std::max)(mi.rcWork.left, mi.rcWork.right - width_px_));
        if (screen_px.y + height_px_ > mi.rcWork.bottom) {
            screen_px.y = (std::max)(mi.rcWork.top, mi.rcWork.bottom - height_px_);
        }
    }
    renderer_->Resize(width_px_, height_px_);
    SetWindowPos(hwnd_, HWND_TOPMOST, screen_px.x, screen_px.y, width_px_, height_px_,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    Log(L"menu shown hwnd=%p visible=%d rect=(%ld,%ld)-(%ld,%ld) dip=%.0fx%.0f nested=%d", hwnd_,
        IsWindowVisible(hwnd_) ? 1 : 0, wr.left, wr.top, wr.right, wr.bottom, width_dip_,
        height_dip_, parent_ ? 1 : 0);

    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    appear_qpc_ = qpc.QuadPart;
    appear_t_ = 0.0f;
    armed_ = true;
    return true;
}

void MenuWindow::DestroyPopup() {
    KillSubmenuTimers();
    CloseSubmenu();
    if (renderer_) {
        renderer_->Shutdown();
        renderer_.reset();
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

int MenuWindow::Run(HWND owner, POINT screen_px) {
    if (!CreatePopup(owner, screen_px)) return -1;

    LARGE_INTEGER start{};
    QueryPerformanceCounter(&start);
    run_qpc_ = start.QuadPart;

    Renderer::FlyoutEnter();
    SetCapture(hwnd_);

    // 从宿主 WM_LBUTTONUP 里开层：排空残留鼠标后立刻上膛，下一次点外面直接关。
    MSG drain{};
    int drained = 0;
    while (PeekMessageW(&drain, nullptr, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
        ++drained;
        Log(L"menu drain msg=0x%x hwnd=%p w=%llu l=%lld", drain.message, drain.hwnd,
            static_cast<unsigned long long>(drain.wParam), static_cast<long long>(drain.lParam));
    }
    Log(L"menu loop start drained=%d armed=1", drained);

    result_ = -1;
    dismissed_ = false;
    POINT cursor{};
    GetCursorPos(&cursor);
    RoutePointer(cursor, true, false, false);
    MSG message{};
    while (!dismissed_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_QUIT) {
            Log(L"menu WM_QUIT");
            PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        const bool mouse_move = message.message == WM_MOUSEMOVE;
        const bool mouse_up = message.message == WM_LBUTTONUP;
        const bool mouse_down = message.message == WM_LBUTTONDOWN ||
                                message.message == WM_LBUTTONDBLCLK ||
                                message.message == WM_RBUTTONDOWN ||
                                message.message == WM_MBUTTONDOWN ||
                                message.message == WM_NCLBUTTONDOWN ||
                                message.message == WM_NCRBUTTONDOWN ||
                                message.message == WM_NCMBUTTONDOWN;
        if (mouse_move || mouse_down || mouse_up) {
            POINT screen{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
            const bool nc = message.message == WM_NCLBUTTONDOWN ||
                            message.message == WM_NCRBUTTONDOWN ||
                            message.message == WM_NCMBUTTONDOWN;
            if (!nc) {
                HWND src = message.hwnd ? message.hwnd : hwnd_;
                if (src) ClientToScreen(src, &screen);
            }
            RoutePointer(screen, mouse_move, mouse_down, mouse_up);
            continue;
        }
        if (message.message == WM_MOUSEWHEEL) {
            POINT screen{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
            if (MenuWindow* hit = HitDeepest(screen)) {
                const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(message.wParam)) /
                                    static_cast<float>(WHEEL_DELTA);
                hit->ScrollBy(steps * theme_.menu_item_height * 3.0f);
            }
            continue;
        }
        TranslateMessage(&message);
        if (message.message == WM_CHAR && on_char_ &&
            on_char_(static_cast<wchar_t>(message.wParam))) {
            Reload();
            if (row_item_.empty()) Dismiss(-1, L"filter_empty");
            continue;
        }
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
            HandleKey(message.wParam);
            continue;
        }
        if (message.message == WM_CHAR) {
            MenuWindow* leaf = this;
            while (leaf->child_) leaf = leaf->child_.get();
            if (leaf->MatchMnemonic(static_cast<wchar_t>(message.wParam))) continue;
            if (hwnd_) SendMessageW(hwnd_, message.message, message.wParam, message.lParam);
            continue;
        }
        DispatchMessageW(&message);
    }
    ReleaseCapture();
    LARGE_INTEGER end{}, freq{};
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&freq);
    const float lived_ms = 1000.0f * static_cast<float>(end.QuadPart - run_qpc_) /
                           static_cast<float>(freq.QuadPart);
    Log(L"menu loop exit result=%d lived_ms=%.1f", result_, lived_ms);
    DestroyPopup();
    Renderer::FlyoutLeave();
    return result_;
}

LRESULT CALLBACK MenuWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<MenuWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
    return self->Handle(hwnd, msg, wparam, lparam);
}

void MenuWindow::TickAppear() {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float dt = static_cast<float>(now.QuadPart - appear_qpc_) /
                     static_cast<float>(frequency.QuadPart);
    appear_qpc_ = now.QuadPart;
    if (appear_t_ >= 1.0f) return;
    appear_t_ = std::min(1.0f, appear_t_ + std::clamp(dt, 0.0f, 0.05f) / kAppearSeconds);
}

LRESULT MenuWindow::Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        const float x = GET_X_LPARAM(lparam) / scale_;
        const float y = GET_Y_LPARAM(lparam) / scale_;
        SetHover(RowAt(x, y));
        return 0;
    }
    case WM_MOUSELEAVE:
        SetHover(-1);
        return 0;
    case WM_LBUTTONDOWN: {
        const float x = GET_X_LPARAM(lparam) / scale_;
        const float y = GET_Y_LPARAM(lparam) / scale_;
        Log(L"menu WM_LBUTTONDOWN xy=(%.1f,%.1f) armed=%d size=%.0fx%.0f", x, y, armed_ ? 1 : 0,
            width_dip_, height_dip_);
        if (!armed_) return 0;
        const int row = RowAt(x, y);
        if (row < 0) {
            Dismiss(-1, L"lbuttondown_outside");
            return 0;
        }
        press_row_ = row;
        return 0;
    }
    case WM_LBUTTONUP:
        Log(L"menu WM_LBUTTONUP armed=%d press_row=%d", armed_ ? 1 : 0, press_row_);
        if (!armed_) {
            armed_ = true;
            return 0;
        }
        if (press_row_ >= 0) CommitRow(press_row_);
        return 0;
    case WM_MOUSEWHEEL: {
        const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                            static_cast<float>(WHEEL_DELTA);
        ScrollBy(steps * theme_.menu_item_height * 3.0f);
        return 0;
    }
    case WM_KEYDOWN:
        HandleKey(wparam);
        return 0;
    case WM_TIMER:
        OnSubmenuTimer(static_cast<UINT_PTR>(wparam));
        return 0;
    case WM_KILLFOCUS:
        Log(L"menu WM_KILLFOCUS armed=%d", armed_ ? 1 : 0);
        if (armed_) Root()->Dismiss(-1, L"killfocus");
        return 0;
    case WM_ACTIVATEAPP:
        Log(L"menu WM_ACTIVATEAPP w=%u armed=%d", static_cast<unsigned>(wparam), armed_ ? 1 : 0);
        if (wparam == FALSE && armed_) Root()->Dismiss(-1, L"activateapp");
        return 0;
    case WM_CAPTURECHANGED: {
        const HWND neu = reinterpret_cast<HWND>(lparam);
        Log(L"menu WM_CAPTURECHANGED new=%p self=%p", reinterpret_cast<void*>(lparam), hwnd_);
        if (!Root()->dismissed_ && neu && neu != Root()->hwnd_) {
            bool ours = false;
            for (MenuWindow* p = Root(); p; p = p->child_.get()) {
                if (p->hwnd_ == neu) {
                    ours = true;
                    break;
                }
            }
            if (!ours) Root()->Dismiss(-1, L"capture_lost");
        }
        return 0;
    }
    case WM_DESTROY:
        Log(L"menu WM_DESTROY");
        hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

MenuWindow* MenuWindow::Root() noexcept {
    MenuWindow* p = this;
    while (p->parent_) p = p->parent_;
    return p;
}

MenuWindow* MenuWindow::HitDeepest(POINT screen_px) {
    if (child_) {
        if (MenuWindow* hit = child_->HitDeepest(screen_px)) return hit;
    }
    return ContainsScreenPx(screen_px) ? this : nullptr;
}

bool MenuWindow::ContainsCascadePx(POINT screen_px) const {
    if (ContainsScreenPx(screen_px)) return true;
    return child_ && child_->ContainsCascadePx(screen_px);
}

void MenuWindow::ToClientDip(POINT screen_px, float& x, float& y) const {
    POINT p = screen_px;
    if (hwnd_) ScreenToClient(hwnd_, &p);
    x = static_cast<float>(p.x) / scale_;
    y = static_cast<float>(p.y) / scale_;
}

void MenuWindow::RoutePointer(POINT screen_px, bool move, bool down, bool up) {
    if (down && !ContainsCascadePx(screen_px)) {
        Log(L"menu click_outside screen=(%ld,%ld)", screen_px.x, screen_px.y);
        Dismiss(-1, L"click_outside");
        return;
    }
    MenuWindow* hit = HitDeepest(screen_px);
        if (!hit) {
            if (move) {
                for (MenuWindow* p = this; p; p = p->child_.get()) {
                    if (p->child_ && p->InSafeTriangle(screen_px)) return;
                }
                MenuWindow* leaf = this;
                while (leaf->child_) leaf = leaf->child_.get();
                leaf->SetHover(-1);
            }
            return;
        }
    float x = 0.0f, y = 0.0f;
    hit->ToClientDip(screen_px, x, y);
    hit->last_cursor_ = screen_px;
    const int row = hit->RowAt(x, y);
    if (move) hit->SetHover(row);
    if (down) {
        if (!hit->armed_) return;
        hit->press_row_ = row;
    }
    if (up) {
        if (!hit->armed_) {
            hit->armed_ = true;
            return;
        }
        if (hit->press_row_ >= 0) hit->CommitRow(hit->press_row_);
        hit->press_row_ = -1;
    }
}

void MenuWindow::HandleKey(WPARAM vk) {
    Log(L"menu WM_KEYDOWN vk=%u armed=%d nested=%d", static_cast<unsigned>(vk), armed_ ? 1 : 0,
        parent_ ? 1 : 0);
    MenuWindow* target = this;
    while (target->child_) target = target->child_.get();
    switch (vk) {
    case VK_ESCAPE:
        Root()->Dismiss(-1, L"escape");
        break;
    case VK_LEFT:
        if (target->parent_) target->parent_->CloseSubmenu();
        break;
    case VK_RETURN:
    case VK_RIGHT:
        if (target->hover_row_ >= 0) target->CommitRow(target->hover_row_);
        break;
    case VK_PRIOR:
        target->ScrollBy(target->height_dip_ - theme_.menu_item_height);
        break;
    case VK_NEXT:
        target->ScrollBy(-(target->height_dip_ - theme_.menu_item_height));
        break;
    case VK_HOME:
        target->scroll_y_ = 0.0f;
        target->ClampScroll();
        if (target->hwnd_) InvalidateRect(target->hwnd_, nullptr, FALSE);
        break;
    case VK_END:
        target->scroll_y_ = target->max_scroll_y_;
        if (target->hwnd_) InvalidateRect(target->hwnd_, nullptr, FALSE);
        break;
    case VK_UP:
    case VK_DOWN: {
        const int direction = vk == VK_UP ? -1 : 1;
        int row = target->hover_row_;
        for (int step = 0; step < static_cast<int>(target->row_item_.size()); ++step) {
            row += direction;
            if (row < 0) row = static_cast<int>(target->row_item_.size()) - 1;
            if (row >= static_cast<int>(target->row_item_.size())) row = 0;
            if (target->row_item_[row] >= 0 &&
                target->RowInteractive(static_cast<size_t>(row))) {
                break;
            }
        }
        target->SetHover(row, true);
        break;
    }
    default:
        target->MatchShortcut(static_cast<uint32_t>(vk));
        break;
    }
}

void MenuWindow::SetHover(int row, bool immediate) {
    POINT cursor{};
    GetCursorPos(&cursor);
    last_cursor_ = cursor;
    if (!immediate && child_ && row != child_row_ && InSafeTriangle(cursor)) return;
    if (hover_row_ == row) {
        SyncSubmenu(immediate);
        return;
    }
    Log(L"menu hover %d -> %d", hover_row_, row);
    hover_row_ = row;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    SyncSubmenu(immediate);
}

void MenuWindow::KillSubmenuTimers() {
    pending_open_row_ = -1;
    pending_close_ = false;
    if (hwnd_) {
        KillTimer(hwnd_, kTimerOpen);
        KillTimer(hwnd_, kTimerClose);
    }
}

void MenuWindow::ArmOpenTimer(int row) {
    pending_close_ = false;
    if (hwnd_) KillTimer(hwnd_, kTimerClose);
    pending_open_row_ = row;
    if (hwnd_) SetTimer(hwnd_, kTimerOpen, kOpenDelayMs, nullptr);
}

void MenuWindow::ArmCloseTimer() {
    pending_open_row_ = -1;
    if (hwnd_) KillTimer(hwnd_, kTimerOpen);
    pending_close_ = true;
    if (hwnd_) SetTimer(hwnd_, kTimerClose, kCloseDelayMs, nullptr);
}

void MenuWindow::OnSubmenuTimer(UINT_PTR id) {
    if (id == kTimerOpen) {
        if (hwnd_) KillTimer(hwnd_, kTimerOpen);
        const int row = pending_open_row_;
        pending_open_row_ = -1;
        if (row >= 0 && row == hover_row_) OpenSubmenu(row);
        return;
    }
    if (id == kTimerClose) {
        if (hwnd_) KillTimer(hwnd_, kTimerClose);
        pending_close_ = false;
        POINT cursor{};
        GetCursorPos(&cursor);
        if (child_ && (ContainsCascadePx(cursor) || InSafeTriangle(cursor))) return;
        CloseSubmenu();
    }
}

void MenuWindow::SyncSubmenu(bool immediate) {
    POINT cursor{};
    GetCursorPos(&cursor);
    if (child_ && InSafeTriangle(cursor)) return;
    if (hover_row_ < 0) {
        if (child_) {
            if (immediate) CloseSubmenu();
            else ArmCloseTimer();
        }
        return;
    }
    if (hover_row_ >= static_cast<int>(row_item_.size())) {
        CloseSubmenu();
        return;
    }
    const int item = row_item_[static_cast<size_t>(hover_row_)];
    const bool has_sub = item >= 0 && !items_[static_cast<size_t>(item)].disabled &&
                         !items_[static_cast<size_t>(item)].children.empty();
    if (has_sub) {
        pending_close_ = false;
        if (hwnd_) KillTimer(hwnd_, kTimerClose);
        if (immediate) {
            KillSubmenuTimers();
            OpenSubmenu(hover_row_);
        } else if (child_row_ != hover_row_) {
            ArmOpenTimer(hover_row_);
        }
        return;
    }
    pending_open_row_ = -1;
    if (hwnd_) KillTimer(hwnd_, kTimerOpen);
    if (child_) {
        if (immediate) CloseSubmenu();
        else ArmCloseTimer();
    }
}

bool MenuWindow::InSafeTriangle(POINT screen_px) const {
    if (!child_ || !child_->hwnd_ || child_row_ < 0 || !hwnd_) return false;
    RECT child_wr{};
    GetWindowRect(child_->hwnd_, &child_wr);
    float y = kMenuPad - scroll_y_;
    for (int r = 0; r < child_row_; ++r) y += RowHeightAt(static_cast<size_t>(r));
    const float h = RowHeightAt(static_cast<size_t>(child_row_));
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    const POINT item_tr{wr.right, wr.top + static_cast<LONG>(y * scale_)};
    const POINT item_br{wr.right, wr.top + static_cast<LONG>((y + h) * scale_)};
    const POINT item_tl{wr.left, item_tr.y};
    const POINT item_bl{wr.left, item_br.y};
    const bool right_of = child_wr.left + 8 >= wr.right - 8;
    if (right_of) {
        const POINT c0{child_wr.left, child_wr.top};
        const POINT c1{child_wr.left, child_wr.bottom};
        return PointInTriangle(screen_px, item_tr, c0, c1) ||
               PointInTriangle(screen_px, item_tr, item_br, c1);
    }
    const POINT c0{child_wr.right, child_wr.top};
    const POINT c1{child_wr.right, child_wr.bottom};
    return PointInTriangle(screen_px, item_tl, c0, c1) ||
           PointInTriangle(screen_px, item_tl, item_bl, c1);
}

void MenuWindow::CommitRow(int row) {
    if (row < 0 || row >= static_cast<int>(row_item_.size())) return;
    const int item = row_item_[static_cast<size_t>(row)];
    if (item < 0 || items_[static_cast<size_t>(item)].disabled ||
        items_[static_cast<size_t>(item)].header) {
        return;
    }
    if (!items_[static_cast<size_t>(item)].children.empty()) {
        OpenSubmenu(row);
        return;
    }
    MenuItem& picked = items_[static_cast<size_t>(item)];
    if (picked.radio) ApplyRadio(items_, static_cast<size_t>(item));
    else if (picked.checkable) picked.checked = !picked.checked;
    if (parent_) {
        if (picked.action) picked.action();
        Root()->Dismiss(-1, L"submenu");
        return;
    }
    result_ = item;
    Dismiss(item, L"commit");
}

bool MenuWindow::MatchMnemonic(wchar_t ch) {
    if (ch < 0x20) return false;
    const wchar_t key = static_cast<wchar_t>(towupper(ch));
    int found = -1;
    for (size_t row = 0; row < row_item_.size(); ++row) {
        if (!RowInteractive(row)) continue;
        const int item = row_item_[row];
        if (MenuAccessKey(items_[static_cast<size_t>(item)].text) != key) continue;
        found = static_cast<int>(row);
        break;
    }
    if (found < 0) return false;
    SetHover(found, true);
    CommitRow(found);
    return true;
}

void MenuWindow::Dismiss(int result, const wchar_t* reason) {
    Log(L"menu Dismiss reason=%s result=%d armed=%d", reason ? reason : L"?", result,
        armed_ ? 1 : 0);
    result_ = result;
    dismissed_ = true;
}

void MenuWindow::Paint() {
    if (!renderer_) return;
    TickAppear();
    ID2D1DeviceContext2* dc = renderer_->BeginDraw();
    if (first_paint_) {
        Log(L"menu Paint first BeginDraw=%d appear=%.2f", dc ? 1 : 0, appear_t_);
    }
    if (!dc) return;
    dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    Painter painter;
    painter.BeginFrame(dc, &UiText(), scale_);
    painter.SetLumaText(renderer_->Luma());
    painter.SetBackdrop(theme_.fill_input);

    const float e = EaseAt(appear_t_, Ease::CssEaseOut);
    const float opacity = e;

    const Rect surface{0.0f, 0.0f, width_dip_, height_dip_};
    const Color fill = Fade(theme_.fill_input, opacity);
    DrawElevated(painter, theme_, surface, theme_.radius_control, Elevation::Overlay, fill, false);

    const float text_x = any_gutter_ ? kGutter : kItemPadX;

    float cursor = kMenuPad - scroll_y_;
    LumaTextBridge* luma = renderer_->Luma();
    for (size_t row = 0; row < row_item_.size(); ++row) {
        const int item_index = row_item_[row];
        if (item_index < 0) {
            painter.FillRect({12.0f, cursor + 4.0f, width_dip_ - 24.0f, 1.0f},
                             Fade(theme_.stroke_divider, opacity));
            cursor += 9.0f;
            continue;
        }
        const MenuItem& item = items_[item_index];
        const float row_h = item.header ? kHeaderHeight : theme_.menu_item_height;
        const Rect row_rect{0.0f, cursor, width_dip_, row_h};
        if (item.header) {
            DrawAccessLabel(painter, item.text,
                            {text_x, cursor, width_dip_ - text_x - kItemRightPad, row_h},
                            TextRole::CaptionStrong, Fade(theme_.text_secondary, opacity),
                            Align::Leading, luma);
            cursor += row_h;
            continue;
        }
        const bool selected = item.checked;
        const bool hot = static_cast<int>(row) == hover_row_ && !item.disabled;
        if (hot) {
            painter.FillRoundedRect(row_rect.Inset(4.0f, 2.0f), 6.0f,
                                    Fade(theme_.fill_hover, opacity));
        } else if (selected && (item.checkable || item.radio)) {
            painter.FillRoundedRect(row_rect.Inset(4.0f, 2.0f), 6.0f,
                                    Fade(theme_.fill_pressed, opacity));
        }
        Color text_color = item.disabled ? theme_.text_disabled
                                         : (selected || hot ? theme_.text : theme_.text_secondary);
        text_color = Fade(text_color, opacity);

        if (any_gutter_) {
            const Rect gutter{8.0f, cursor, 16.0f, row_h};
            const float cx = 16.0f;
            const float cy = cursor + row_h * 0.5f;
            if (item.radio) {
                painter.StrokeRoundedRect({cx - 5.0f, cy - 5.0f, 10.0f, 10.0f}, 5.0f,
                                          Fade(theme_.text_secondary, opacity), 1.2f);
                if (item.checked) {
                    painter.FillRoundedRect({cx - 3.0f, cy - 3.0f, 6.0f, 6.0f}, 3.0f, text_color);
                }
            } else if (item.checked) {
                painter.DrawIcon(icon::kCheckMark, gutter, 14.0f, Fade(theme_.text, opacity));
            } else if (!item.glyph.empty()) {
                painter.DrawIcon(item.glyph, gutter, 16.0f, Fade(theme_.text_secondary, opacity));
            }
        }

        float text_width = width_dip_ - text_x - kItemRightPad;
        if (!item.shortcut.empty()) {
            const Size shortcut_size = painter.MeasureText(item.shortcut, TextRole::Caption);
            text_width -= shortcut_size.w + 16.0f;
            painter.DrawText(item.shortcut,
                             {text_x, cursor, width_dip_ - kItemRightPad - 8.0f - text_x, row_h},
                             TextRole::Caption, Fade(theme_.text_secondary, opacity),
                             Align::Trailing);
        }
        if (!item.children.empty()) text_width -= 18.0f;
        DrawAccessLabel(painter, item.text, {text_x, cursor, text_width, row_h}, TextRole::Body,
                        text_color, Align::Leading, luma);
        if (!item.children.empty() && !item.disabled) {
            painter.DrawChevron({width_dip_ - 18.0f, cursor + row_h * 0.5f}, 12.0f, -90.0f,
                                Fade(theme_.text_secondary, opacity), 1.4f);
        }
        cursor += row_h;
    }
    painter.EndFrame();
    // 整层缩放/位移交给 DComp，绘制保持恒等，LumaText 不会切回 DirectWrite。
    if (appear_t_ < 1.0f) {
        const float s = Lerp(0.94f, 1.0f, e);
        const float ty = Lerp(8.0f, 0.0f, e) * scale_;
        const D2D1_MATRIX_3X2_F m =
            D2D1::Matrix3x2F::Scale(s, s, D2D1::Point2F(static_cast<float>(width_px_) * 0.5f, 0.0f)) *
            D2D1::Matrix3x2F::Translation(0.0f, ty);
        renderer_->SetVisualTransform(m);
    } else {
        renderer_->SetVisualTransform(D2D1::Matrix3x2F::Identity());
    }
    const bool presented = renderer_->EndDraw(true);
    if (first_paint_) {
        first_paint_ = false;
        Log(L"menu Paint first EndDraw=%d appear=%.2f", presented ? 1 : 0, appear_t_);
        if (hwnd_) {
            SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    if (appear_t_ < 1.0f && hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}


void MenuWindow::ClampScroll() {
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll_y_);
}

void MenuWindow::ScrollBy(float delta) {
    if (max_scroll_y_ <= 0.5f) return;
    scroll_y_ = std::clamp(scroll_y_ - delta, 0.0f, max_scroll_y_);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

bool MenuWindow::MatchShortcut(uint32_t vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    auto parse = [&](const std::wstring& s) -> bool {
        if (s.empty()) return false;
        bool want_ctrl = false, want_shift = false, want_alt = false;
        std::wstring key;
        size_t i = 0;
        while (i < s.size()) {
            if (s.compare(i, 5, L"Ctrl+") == 0 || s.compare(i, 5, L"CTRL+") == 0) {
                want_ctrl = true; i += 5; continue;
            }
            if (s.compare(i, 6, L"Shift+") == 0 || s.compare(i, 6, L"SHIFT+") == 0) {
                want_shift = true; i += 6; continue;
            }
            if (s.compare(i, 4, L"Alt+") == 0 || s.compare(i, 4, L"ALT+") == 0) {
                want_alt = true; i += 4; continue;
            }
            key = s.substr(i);
            break;
        }
        if (want_ctrl != ctrl || want_shift != shift || want_alt != alt) return false;
        if (key.size() == 1) {
            return vk == static_cast<uint32_t>(towupper(key[0]));
        }
        if (key == L"Enter" || key == L"Return") return vk == VK_RETURN;
        if (key.size() >= 2 && (key[0] == L'F' || key[0] == L'f')) {
            const int n = _wtoi(key.c_str() + 1);
            if (n >= 1 && n <= 24) return vk == static_cast<uint32_t>(VK_F1 + n - 1);
        }
        return false;
    };
    for (size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& it = items_[i];
        if (it.separator || it.disabled || !it.children.empty()) continue;
        if (!parse(it.shortcut)) continue;
        if (parent_) {
            if (it.action) it.action();
            Root()->Dismiss(-1, L"shortcut");
            return true;
        }
        result_ = static_cast<int>(i);
        Dismiss(static_cast<int>(i), L"shortcut");
        return true;
    }
    return false;
}

void MenuWindow::CloseSubmenu() {
    if (!child_) return;
    Log(L"menu CloseSubmenu row=%d", child_row_);
    child_->CloseSubmenu();
    child_->DestroyPopup();
    child_.reset();
    child_row_ = -1;
}

void MenuWindow::OpenSubmenu(int row) {
    if (row < 0 || row >= static_cast<int>(row_item_.size())) return;
    const int item = row_item_[static_cast<size_t>(row)];
    if (item < 0 || items_[static_cast<size_t>(item)].children.empty()) return;
    if (child_row_ == row && child_) return;
    CloseSubmenu();
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    float y = kMenuPad - scroll_y_;
    for (int r = 0; r < row; ++r) {
        y += RowHeightAt(static_cast<size_t>(r));
    }
    POINT sp{wr.right - 4, wr.top + static_cast<LONG>(y * scale_)};
    child_.reset(new MenuWindow(items_[static_cast<size_t>(item)].children, theme_, scale_, 0.0f));
    child_->parent_ = this;
    if (!child_->CreatePopup(hwnd_, sp)) {
        child_.reset();
        return;
    }
    child_row_ = row;
    Log(L"menu OpenSubmenu row=%d", row);
}


int Menu::PopupTo(Control& anchor) {
    Window* window = anchor.WindowOf();
    if (!window) {
#if defined(_DEBUG)
        OutputDebugStringW(L"[lumen] Menu::PopupTo: anchor is not in a window tree\n");
#endif
        return -1;
    }
    const Rect r = anchor.AbsoluteBounds();
    return Popup(*window, {r.x, r.Bottom()});
}

int Menu::Popup(Window& window, Point client_point) {
    auto sync = [](auto& self, std::vector<MenuItem>& items) -> void {
        for (auto& item : items) {
            if (item.command) item.disabled = !item.command->Enabled();
            if (!item.children.empty()) self(self, item.children);
        }
    };
    sync(sync, items_);
    WindowImpl* impl = window.Impl();
    if (!impl) return -1;
    POINT px{static_cast<LONG>(client_point.x * impl->Scale()),
             static_cast<LONG>(client_point.y * impl->Scale())};
    ClientToScreen(impl->Hwnd(), &px);
    const int result = MenuWindow::Show(impl->Hwnd(), items_, px, impl->ThemeRef(), impl->Scale());
    if (result >= 0 && result < static_cast<int>(items_.size()) &&
        items_[static_cast<size_t>(result)].action) {
        items_[static_cast<size_t>(result)].action();
    }
    window.Invalidate();
    return result;
}

MenuItem& Menu::AddItem(std::wstring_view text, std::function<void()> action) {
    MenuItem item;
    item.text = std::wstring(text);
    item.action = std::move(action);
    items_.push_back(std::move(item));
    return items_.back();
}

MenuItem& Menu::AddItem(MenuItem item) {
    items_.push_back(std::move(item));
    return items_.back();
}

MenuItem& Menu::Add(Command& command) {
    MenuItem item;
    item.text = command.Label();
    item.glyph = command.Glyph();
    item.shortcut = command.Shortcut();
    item.disabled = !command.Enabled();
    item.command = &command;
    item.action = [&command] { command.Execute(); };
    items_.push_back(std::move(item));
    return items_.back();
}

MenuItem& Menu::AddSeparator() {
    MenuItem item;
    item.separator = true;
    items_.push_back(std::move(item));
    return items_.back();
}

MenuItem& MenuItem::AddChild(std::wstring_view child_text, std::function<void()> child_action) {
    MenuItem child;
    child.text = std::wstring(child_text);
    child.action = std::move(child_action);
    children.push_back(std::move(child));
    return children.back();
}

MenuItem MenuItem::Header(std::wstring_view text) {
    MenuItem item;
    item.text = std::wstring(text);
    item.header = true;
    item.disabled = true;
    return item;
}

MenuItem MenuItem::Sub(std::wstring_view text) {
    MenuItem item;
    item.text = std::wstring(text);
    return item;
}

MenuItem& Menu::AddHeader(std::wstring_view text) { return AddItem(MenuItem::Header(text)); }

} // namespace lumen
