// frame_chrome.cpp — WindowImpl 客户区边框、标题栏命中、DWM 属性。
#include "window_impl.h"
#include "lumen/TitleBar.h"
#include <dwmapi.h>
#include <windowsx.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFEu
#endif

namespace lumen {
namespace {
constexpr float kCaptionDip = 40.0f;
constexpr float kCaptionButtonDip = 46.0f;
}

DWORD WindowImpl::FrameStyle() const {
    if (frame_ == Frame::Client) {
        return WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    }
    return WS_OVERLAPPEDWINDOW;
}

void WindowImpl::AdjustFrameRect(RECT* rect) const {
    if (frame_ == Frame::System) {
        AdjustWindowRectExForDpi(rect, FrameStyle(), FALSE, WS_EX_NOREDIRECTIONBITMAP,
                                 static_cast<UINT>(scale_ * 96.0f));
    }
}

float WindowImpl::CaptionHeight() const noexcept {
    if (title_bar_) return title_bar_->Height();
    return frame_ == Frame::Client ? kCaptionDip : 0.0f;
}

void WindowImpl::ApplyClientChrome() {
    if (frame_ != Frame::Client || !hwnd_) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    const COLORREF border = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    const MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

LRESULT WindowImpl::CaptionButtonAt(POINT client_px) const {
    const float bw = kCaptionButtonDip * scale_;
    const float w = static_cast<float>(client_w_);
    const float x = static_cast<float>(client_px.x);
    if (x >= w - bw) return HTCLOSE;
    if (x >= w - 2.0f * bw) return HTMAXBUTTON;
    if (x >= w - 3.0f * bw) return HTMINBUTTON;
    return 0;
}

LRESULT WindowImpl::HitTestFrame(LPARAM lparam) const {
    POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    if (!IsZoomed(hwnd_)) {
        RECT window{};
        GetWindowRect(hwnd_, &window);
        const UINT dpi = GetDpiForWindow(hwnd_);
        const int frame_x = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const int frame_y = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const bool left = screen.x >= window.left && screen.x < window.left + frame_x;
        const bool right = screen.x < window.right && screen.x >= window.right - frame_x;
        const bool top = screen.y >= window.top && screen.y < window.top + frame_y;
        const bool bottom = screen.y < window.bottom && screen.y >= window.bottom - frame_y;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    POINT client = screen;
    ScreenToClient(hwnd_, &client);
    const Point dip{static_cast<float>(client.x) / scale_, static_cast<float>(client.y) / scale_};
    const float caption = CaptionHeight();
    if (dip.y >= 0.0f && dip.y < caption) {
        if (title_bar_) {
            switch (title_bar_->Hit(dip)) {
            case TitleBar::Region::Min: return HTMINBUTTON;
            case TitleBar::Region::Max: return HTMAXBUTTON;
            case TitleBar::Region::Close: return HTCLOSE;
            default: break;
            }
            Control* hit = HitTree(title_bar_.get(), dip);
            if (hit && hit != title_bar_.get()) return HTCLIENT;
        }
        return HTCAPTION;
    }
    return HTCLIENT;
}

void WindowImpl::SetCaptionHover(LRESULT hit) {
    CaptionHover hover = CaptionHover::None;
    if (hit == HTMINBUTTON) hover = CaptionHover::Min;
    else if (hit == HTMAXBUTTON) hover = CaptionHover::Max;
    else if (hit == HTCLOSE) hover = CaptionHover::Close;
    if (title_bar_) {
        TitleBar::Region region = TitleBar::Region::Caption;
        if (hover == CaptionHover::Min) region = TitleBar::Region::Min;
        else if (hover == CaptionHover::Max) region = TitleBar::Region::Max;
        else if (hover == CaptionHover::Close) region = TitleBar::Region::Close;
        title_bar_->SetButtonHover(region);
    }
    if (hover == caption_hover_) return;
    caption_hover_ = hover;
    Invalidate();
}

void WindowImpl::TrackNcMouse() {
    if (tracking_nc_mouse_) return;
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd_, 0};
    TrackMouseEvent(&track);
    tracking_nc_mouse_ = true;
}

void WindowImpl::DrawCaption(const Rect&) {
    if (!title_bar_) return;
    title_bar_->Maximized(hwnd_ && IsZoomed(hwnd_) != FALSE);
    DrawTree(title_bar_.get());
}

} // namespace lumen
