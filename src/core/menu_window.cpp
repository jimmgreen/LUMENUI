#include "menu_window.h"
#include "fluentui/Menu.h"
#include "fluentui/Painter.h"
#include "renderer.h"
#include "text_service.h"
#include "window_impl.h"
#include <windowsx.h>
#include <algorithm>

namespace fui {
namespace {

constexpr float kMenuPad = 8.0f;
constexpr float kItemTextX = 38.0f;
constexpr float kItemRightPad = 12.0f;

} // namespace

MenuWindow::MenuWindow(const std::vector<MenuItem>& items, const Theme& theme, float scale)
    : items_(items), theme_(theme), scale_(scale) {}

void MenuWindow::ComputeLayout() {
    row_item_.clear();
    float max_text_w = 0.0f, max_shortcut_w = 0.0f;
    bool any_shortcut = false;
    for (const MenuItem& item : items_) {
        if (item.separator) continue;
        max_text_w = std::max(max_text_w, UiText().MeasureText(item.text, TextRole::Body).w);
        if (!item.shortcut.empty()) {
            any_shortcut = true;
            max_shortcut_w = std::max(max_shortcut_w,
                                      UiText().MeasureText(item.shortcut, TextRole::Caption).w);
        }
    }
    width_dip_ = std::max(kItemTextX + max_text_w + kItemRightPad * 2.0f +
                              (any_shortcut ? max_shortcut_w + 16.0f : 0.0f),
                          180.0f);
    height_dip_ = kMenuPad;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].separator) {
            row_item_.push_back(-1);
            height_dip_ += 9.0f;
        } else {
            row_item_.push_back(static_cast<int>(i));
            height_dip_ += theme_.menu_item_height;
        }
    }
    height_dip_ += kMenuPad;
}

int MenuWindow::Show(HWND owner, const std::vector<MenuItem>& items, POINT screen_px,
                     const Theme& theme, float scale) {
    MenuWindow menu(items, theme, scale);
    return menu.Run(owner, screen_px);
}

int MenuWindow::Run(HWND owner, POINT screen_px) {
    ComputeLayout();
    if (row_item_.empty()) return -1;

    width_px_ = static_cast<int>(width_dip_ * scale_);
    height_px_ = static_cast<int>(height_dip_ * scale_);

    // 收进最近显示器工作区
    HMONITOR monitor = MonitorFromPoint(screen_px, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(monitor, &info)) {
        screen_px.x = std::clamp(screen_px.x, info.rcWork.left,
                                 std::max(info.rcWork.left, info.rcWork.right - width_px_));
        if (screen_px.y + height_px_ > info.rcWork.bottom) {
            screen_px.y = std::max(info.rcWork.top, info.rcWork.bottom - height_px_);
        }
    }
    (void)owner;

    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MenuWindow::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"fui_menu";
        return RegisterClassExW(&wc) != 0;
    }();
    (void)registered;

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"fui_menu", L"", WS_POPUP,
                            screen_px.x, screen_px.y, width_px_, height_px_, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return -1;
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->Init(hwnd_, width_px_, height_px_)) {
        renderer_->Shutdown();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return -1;
    }
    ShowWindow(hwnd_, SW_SHOW);
    SetCapture(hwnd_);

    result_ = -1;
    dismissed_ = false;
    MSG message{};
    while (!dismissed_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ReleaseCapture();
    renderer_->Shutdown();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
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
    case WM_MOUSEMOVE: {
        const float y = GET_Y_LPARAM(lparam) / scale_;
        int row = -1;
        float cursor = kMenuPad;
        for (size_t i = 0; i < row_item_.size(); ++i) {
            const float row_h = row_item_[i] < 0 ? 9.0f : theme_.menu_item_height;
            if (y >= cursor && y < cursor + row_h) {
                row = static_cast<int>(i);
                break;
            }
            cursor += row_h;
        }
        if (row >= 0 && row_item_[row] >= 0 && items_[row_item_[row]].disabled) row = -1;
        SetHover(row);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const float x = GET_X_LPARAM(lparam) / scale_;
        const float y = GET_Y_LPARAM(lparam) / scale_;
        if (x < 0.0f || x > width_dip_ || y < 0.0f || y > height_dip_) {
            Dismiss(-1);
            return 0;
        }
        // 与 WM_MOUSEMOVE 相同的行定位
        int row = -1;
        float cursor = kMenuPad;
        for (size_t i = 0; i < row_item_.size(); ++i) {
            const float row_h = row_item_[i] < 0 ? 9.0f : theme_.menu_item_height;
            if (y >= cursor && y < cursor + row_h) {
                row = static_cast<int>(i);
                break;
            }
            cursor += row_h;
        }
        press_row_ = (row >= 0 && row_item_[row] >= 0 && !items_[row_item_[row]].disabled) ? row : -1;
        return 0;
    }
    case WM_LBUTTONUP:
        if (press_row_ >= 0) CommitRow(press_row_);
        else Dismiss(-1);
        return 0;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_ESCAPE:
            Dismiss(-1);
            break;
        case VK_RETURN:
            if (hover_row_ >= 0) CommitRow(hover_row_);
            break;
        case VK_UP:
        case VK_DOWN: {
            const int direction = wparam == VK_UP ? -1 : 1;
            int row = hover_row_;
            for (int step = 0; step < static_cast<int>(row_item_.size()); ++step) {
                row += direction;
                if (row < 0) row = static_cast<int>(row_item_.size()) - 1;
                if (row >= static_cast<int>(row_item_.size())) row = 0;
                if (row_item_[row] >= 0 && !items_[row_item_[row]].disabled) break;
            }
            SetHover(row);
            break;
        }
        default:
            break;
        }
        return 0;
    case WM_KILLFOCUS:
        Dismiss(-1);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void MenuWindow::SetHover(int row) {
    if (hover_row_ == row) return;
    hover_row_ = row;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void MenuWindow::CommitRow(int row) {
    if (row < 0 || row >= static_cast<int>(row_item_.size())) return;
    const int item = row_item_[row];
    if (item < 0 || items_[item].disabled) return;
    result_ = item;
    Dismiss(item);
}

void MenuWindow::Dismiss(int result) {
    result_ = result;
    dismissed_ = true;
}

void MenuWindow::Paint() {
    if (!renderer_) return;
    ID2D1DeviceContext2* dc = renderer_->BeginDraw();
    if (!dc) return;
    Painter painter;
    painter.BeginFrame(dc, &UiText(), scale_);
    const Rect surface{0.0f, 0.0f, width_dip_, height_dip_};
    painter.FillRoundedRect(surface, theme_.radius_flyout, theme_.flyout);
    painter.StrokeRoundedRect(surface, theme_.radius_flyout, theme_.control_stroke_strong);

    float cursor = kMenuPad;
    for (size_t row = 0; row < row_item_.size(); ++row) {
        const int item_index = row_item_[row];
        if (item_index < 0) {
            painter.FillRect({4.0f, cursor + 4.0f, width_dip_ - 8.0f, 1.0f}, theme_.divider);
            cursor += 9.0f;
            continue;
        }
        const MenuItem& item = items_[item_index];
        const Rect row_rect{0.0f, cursor, width_dip_, theme_.menu_item_height};
        const Color text_color = item.disabled ? theme_.text_disabled : theme_.text;
        if (static_cast<int>(row) == hover_row_ && !item.disabled) {
            painter.FillRoundedRect(row_rect.Inset(4.0f, 2.0f), theme_.radius_control,
                                    theme_.control_fill_hover);
        }
        if (!item.glyph.empty() || item.checked) {
            painter.DrawIcon(item.checked ? L"\uE73E" : item.glyph.c_str(),
                             {kItemTextX - 26.0f, cursor, 16.0f, theme_.menu_item_height}, 16.0f,
                             item.checked ? theme_.accent : theme_.text_secondary);
        }
        float text_width = width_dip_ - kItemTextX - kItemRightPad;
        if (!item.shortcut.empty()) {
            const Size shortcut_size = UiText().MeasureText(item.shortcut, TextRole::Caption);
            text_width -= shortcut_size.w + 16.0f;
            painter.DrawText(item.shortcut,
                             {kItemTextX, cursor, width_dip_ - kItemRightPad - 8.0f - shortcut_size.w,
                              theme_.menu_item_height},
                             TextRole::Caption, theme_.text_secondary, Align::Leading);
        }
        painter.DrawText(item.text, {kItemTextX, cursor, text_width, theme_.menu_item_height},
                         TextRole::Body, text_color);
        cursor += theme_.menu_item_height;
    }
    painter.EndFrame();
    renderer_->EndDraw();
}

int Menu::Popup(Window& window, Point client_point) {
    WindowImpl* impl = window.Impl();
    if (!impl) return -1;
    POINT px{static_cast<LONG>(client_point.x * impl->Scale()),
             static_cast<LONG>(client_point.y * impl->Scale())};
    ClientToScreen(impl->Hwnd(), &px);
    const int result = MenuWindow::Show(impl->Hwnd(), items_, px, impl->ThemeRef(), impl->Scale());
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

MenuItem& Menu::AddSeparator() {
    MenuItem item;
    item.separator = true;
    items_.push_back(std::move(item));
    return items_.back();
}

} // namespace fui
