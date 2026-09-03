// tray_host.cpp — WindowImpl 托盘图标与最小化到托盘。
#include "window_impl.h"
#include <algorithm>
#include <shellapi.h>

#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif

namespace lumen {

void WindowImpl::InstallTray() {
    if (!hwnd_ || tray_installed_) return;
    tray_ = {};
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = hwnd_;
    tray_.uID = 1;
    tray_.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    tray_.uCallbackMessage = kWmTray;
    if (!tray_.hIcon) tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (Shell_NotifyIconW(NIM_ADD, &tray_)) tray_installed_ = true;
}

void WindowImpl::RemoveTray() {
    if (!tray_installed_) return;
    Shell_NotifyIconW(NIM_DELETE, &tray_);
    tray_installed_ = false;
}

void WindowImpl::RestoreFromTray() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void WindowImpl::TrayIcon(void* hicon, std::wstring_view tooltip) {
    if (hicon) tray_.hIcon = static_cast<HICON>(hicon);
    else tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    tray_.szTip[0] = 0;
    const size_t n = std::min(tooltip.size(), static_cast<size_t>(ARRAYSIZE(tray_.szTip) - 1));
    size_t i = 0;
    for (; i < n; ++i) tray_.szTip[i] = tooltip[i];
    tray_.szTip[i] = 0;
    if (tray_installed_) {
        tray_.hWnd = hwnd_;
        tray_.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
        tray_.uCallbackMessage = kWmTray;
        Shell_NotifyIconW(NIM_MODIFY, &tray_);
    } else {
        InstallTray();
    }
}

void WindowImpl::OnTrayClick(std::function<void()> handler) {
    tray_click_.Subscribe(std::move(handler));
}
Connection WindowImpl::BindTrayClick(std::function<void()> handler) {
    return tray_click_.Connect(std::move(handler));
}

void WindowImpl::MinimizeToTray(bool on) {
    minimize_to_tray_ = on;
    if (on && !tray_installed_) InstallTray();
}

void WindowImpl::SetTrayMenu(Menu menu) {
    tray_menu_ = std::move(menu);
    has_tray_menu_ = !tray_menu_.Empty();
}

} // namespace lumen
