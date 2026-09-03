#include "lumen/App.h"
#include "app_host.h"
#include "hotkey.h"
#include <windows.h>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace lumen {
namespace {

HWND g_main_hwnd = nullptr;
HWND g_msg_hwnd = nullptr;
HANDLE g_single_mutex = nullptr;
std::wstring g_single_name;
UINT g_activate_msg = 0;
int g_hotkey_next = 1;

struct GlobalHotkey {
    int id = 0;
    std::wstring chord;
    uint32_t vk = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    std::function<void()> fn;
};
std::vector<GlobalHotkey> g_hotkeys;

constexpr wchar_t kAppClass[] = L"lumen_app";

LRESULT CALLBACK AppWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_HOTKEY) {
        AppHandleHotkey(wparam);
        return 0;
    }
    if (g_activate_msg && msg == g_activate_msg) {
        if (g_main_hwnd) {
            if (IsIconic(g_main_hwnd)) ShowWindow(g_main_hwnd, SW_RESTORE);
            ShowWindow(g_main_hwnd, SW_SHOW);
            SetForegroundWindow(g_main_hwnd);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void EnsureAppClass() {
    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &AppWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kAppClass;
        return RegisterClassExW(&wc) != 0;
    }();
    (void)registered;
}

HWND EnsureMsgWindow(std::wstring_view title) {
    EnsureAppClass();
    if (g_msg_hwnd) return g_msg_hwnd;
    g_msg_hwnd = CreateWindowExW(0, kAppClass, std::wstring(title).c_str(), 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    return g_msg_hwnd;
}

UINT ModsOf(bool ctrl, bool shift, bool alt) {
    UINT mods = 0;
    if (ctrl) mods |= MOD_CONTROL;
    if (shift) mods |= MOD_SHIFT;
    if (alt) mods |= MOD_ALT;
    mods |= MOD_NOREPEAT;
    return mods;
}

} // namespace

void AppBindWindow(HWND hwnd) {
    if (!g_main_hwnd) g_main_hwnd = hwnd;
}

void AppUnbindWindow(HWND hwnd) {
    if (g_main_hwnd == hwnd) g_main_hwnd = nullptr;
}

void AppActivateExisting() {
    if (g_activate_msg) {
        PostMessageW(HWND_BROADCAST, g_activate_msg, 0, 0);
    }
    if (!g_single_name.empty()) {
        HWND peer = FindWindowW(kAppClass, g_single_name.c_str());
        if (peer) PostMessageW(peer, g_activate_msg ? g_activate_msg : WM_USER, 0, 0);
    }
}

bool AppHandleHotkey(WPARAM id) {
    const int hid = static_cast<int>(id);
    for (const GlobalHotkey& slot : g_hotkeys) {
        if (slot.id == hid && slot.fn) {
            slot.fn();
            return true;
        }
    }
    return false;
}

UINT AppActivateMsg() { return g_activate_msg; }

namespace {
bool g_ensured = false;
Strings g_strings;
bool g_strings_ready = false;
} // namespace

void App::Ensure() {
    if (!g_ensured) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        g_ensured = true;
    }
    if (!g_strings_ready) {
        g_strings = Strings::ForSystem();
        g_strings_ready = true;
    }
}

const Strings& App::Strings() {
    Ensure();
    return g_strings;
}

void App::Strings(lumen::Strings value) {
    Ensure();
    g_strings = std::move(value);
}

App::App() {
    Ensure();
}

App::~App() {
    if (g_msg_hwnd) {
        for (const GlobalHotkey& slot : g_hotkeys) {
            UnregisterHotKey(g_msg_hwnd, slot.id);
        }
        DestroyWindow(g_msg_hwnd);
        g_msg_hwnd = nullptr;
    }
    g_hotkeys.clear();
    if (g_single_mutex) {
        CloseHandle(g_single_mutex);
        g_single_mutex = nullptr;
    }
}

int App::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void App::Quit(int exit_code) {
    PostQuitMessage(exit_code);
}

bool App::SingleInstance(std::wstring_view name) {
    g_single_name = std::wstring(name);
    std::wstring msg_name = L"lumen.activate." + g_single_name;
    g_activate_msg = RegisterWindowMessageW(msg_name.c_str());
    const std::wstring mutex_name = L"Local\\lumen.single." + g_single_name;
    g_single_mutex = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        AppActivateExisting();
        if (g_single_mutex) {
            CloseHandle(g_single_mutex);
            g_single_mutex = nullptr;
        }
        return false;
    }
    EnsureMsgWindow(g_single_name);
    return true;
}

bool App::RegisterGlobalHotkey(std::wstring_view chord, std::function<void()> fn) {
    uint32_t vk = 0;
    bool ctrl = false, shift = false, alt = false;
    if (!ParseChord(chord, vk, ctrl, shift, alt) || vk == 0) return false;
    HWND hwnd = EnsureMsgWindow(g_single_name);
    if (!hwnd) return false;
    UnregisterGlobalHotkey(chord);
    const int id = g_hotkey_next++;
    if (!RegisterHotKey(hwnd, id, ModsOf(ctrl, shift, alt), vk)) return false;
    g_hotkeys.push_back(GlobalHotkey{id, std::wstring(chord), vk, ctrl, shift, alt, std::move(fn)});
    return true;
}

void App::UnregisterGlobalHotkey(std::wstring_view chord) {
    if (!g_msg_hwnd) return;
    for (size_t i = 0; i < g_hotkeys.size(); ++i) {
        if (g_hotkeys[i].chord == chord) {
            UnregisterHotKey(g_msg_hwnd, g_hotkeys[i].id);
            g_hotkeys.erase(g_hotkeys.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }
}

} // namespace lumen
