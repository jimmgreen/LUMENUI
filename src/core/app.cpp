#include "lumen/App.h"
#include "app_host.h"
#include "hotkey.h"
#include "log.h"
#include "lumatext_bridge.h"
#include "text_service.h"
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>
#include <list>
#include <mutex>
#include <thread>

namespace lumen {
namespace {

HWND g_main_hwnd = nullptr;
HWND g_msg_hwnd = nullptr;
HANDLE g_single_mutex = nullptr;
std::wstring g_single_name;
UINT g_activate_msg = 0;
int g_hotkey_next = 1;
bool g_host_mode = false;

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

constexpr const wchar_t* kClassNames[] = {L"lumen_window", L"lumen_app", L"lumen_menu",
                                          L"lumen_popup"};
bool g_class_registered[3] = {false, false, false};

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

HWND EnsureMsgWindow(std::wstring_view title) {
    EnsureLumenClass(LumenClass::App, &AppWndProc, 0, false);
    if (g_msg_hwnd) return g_msg_hwnd;
    g_msg_hwnd = CreateWindowExW(0, LumenClassName(LumenClass::App), std::wstring(title).c_str(),
                                 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, LumenModule(), nullptr);
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

void ReleaseMsgWindow() {
    if (g_msg_hwnd) {
        for (const GlobalHotkey& slot : g_hotkeys) {
            UnregisterHotKey(g_msg_hwnd, slot.id);
        }
        if (IsWindow(g_msg_hwnd) && !DestroyWindow(g_msg_hwnd))
            throw std::runtime_error("LUMEN app message window destruction failed");
        g_msg_hwnd = nullptr;
    }
    g_hotkeys.clear();
    if (g_single_mutex) {
        CloseHandle(g_single_mutex);
        g_single_mutex = nullptr;
    }
}

} // namespace

HINSTANCE LumenModule() {
    static HINSTANCE module = [] {
        HMODULE handle = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&LumenModule), &handle) ||
            !handle) {
            handle = GetModuleHandleW(nullptr);
        }
        return static_cast<HINSTANCE>(handle);
    }();
    return module;
}

const wchar_t* LumenClassName(LumenClass cls) noexcept {
    return kClassNames[static_cast<size_t>(cls)];
}

bool EnsureLumenClass(LumenClass cls, WNDPROC proc, UINT style, bool arrow_cursor) {
    const size_t index = static_cast<size_t>(cls);
    if (g_class_registered[index]) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = style;
    wc.lpfnWndProc = proc;
    wc.hInstance = LumenModule();
    if (arrow_cursor) wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassNames[index];
    ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        UnregisterClassW(kClassNames[index], wc.hInstance);
        atom = RegisterClassExW(&wc);
    }
    g_class_registered[index] = atom != 0;
    if (!atom) Log(LogLevel::Warn, L"RegisterClassExW %s failed: %lu", kClassNames[index], GetLastError());
    return g_class_registered[index];
}

void UnregisterLumenClasses() {
    for (size_t i = 0; i < 3; ++i) {
        if (!g_class_registered[i]) continue;
        if (!UnregisterClassW(kClassNames[i], LumenModule()) &&
            GetLastError() != ERROR_CLASS_DOES_NOT_EXIST)
            throw std::runtime_error("LUMEN window class unregistration failed");
        g_class_registered[i] = false;
    }
}

DpiContextScope::DpiContextScope(HWND match_hwnd) {
    if (match_hwnd && IsWindow(match_hwnd)) {
        const DPI_AWARENESS_CONTEXT ctx = GetWindowDpiAwarenessContext(match_hwnd);
        if (ctx) {
            previous_ = SetThreadDpiAwarenessContext(ctx);
            return;
        }
    }
    if (App::HostMode()) {
        previous_ = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

DpiContextScope::~DpiContextScope() {
    if (previous_) SetThreadDpiAwarenessContext(previous_);
}

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
        HWND peer = FindWindowW(LumenClassName(LumenClass::App), g_single_name.c_str());
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
        if (!g_host_mode) {
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        g_ensured = true;
    }
    if (!g_strings_ready) {
        g_strings = Strings::ForSystem();
        g_strings_ready = true;
    }
}

void App::HostMode(bool on) { g_host_mode = on; }
bool App::HostMode() { return g_host_mode; }

bool UiaCanShutdown();
namespace {
std::atomic<int> g_live_tasks{0};   // 进程级 RunAsync 在跑任务数（任意线程增减）
struct WorkerThreads {
    std::mutex mutex;
    std::list<std::thread> threads;
    ~WorkerThreads() {
        for (auto& thread : threads) if (thread.joinable()) thread.join();
    }
    int Reap() {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = threads.begin(); it != threads.end();) {
            // done 标志仍早于线程函数返回/TLS 析构；只有系统句柄就绪才真正退出。
            if (WaitForSingleObject(it->native_handle(), 0) == WAIT_OBJECT_0) {
                it->join();
                it = threads.erase(it);
            } else ++it;
        }
        return static_cast<int>(threads.size());
    }
};
WorkerThreads g_workers;
}
void AppStartWorker(std::function<void()> work) {
    g_workers.Reap();
    std::lock_guard<std::mutex> lock(g_workers.mutex);
    auto slot = g_workers.threads.emplace(g_workers.threads.end());
    try {
        *slot = std::thread(std::move(work));
    } catch (...) {
        g_workers.threads.erase(slot);
        throw;
    }
}
bool App::CanShutdown() {
    // UIA 无引用且 RunAsync 任务全部结束才允许卸载：任务体可能仍在执行模块代码。
    return UiaCanShutdown() && RunningTasks() == 0;
}
void App::TaskBegin() noexcept { g_live_tasks.fetch_add(1, std::memory_order_relaxed); }
void App::TaskEnd() noexcept { g_live_tasks.fetch_sub(1, std::memory_order_acq_rel); }
int App::RunningTasks() noexcept {
    return g_workers.Reap() + g_live_tasks.load(std::memory_order_acquire);
}

void App::Shutdown() {
    if (!CanShutdown()) throw std::runtime_error("LUMEN is still shutting down: UIA providers or RunAsync tasks are active");
    ReleaseMsgWindow();
    UnregisterLumenClasses();
    LumaTextResetProcessCaches();
    UiText().Reset();
    g_main_hwnd = nullptr;
    g_ensured = false;
    g_strings_ready = false;
}

std::wstring App::AddFont(std::span<const std::byte> data) {
    Ensure();
    return UiText().AddFont(data);
}

std::wstring App::AddFont(std::wstring_view path) {
    Ensure();
    return UiText().AddFontFile(path);
}

void App::LumaTextLibrary(std::wstring_view path) {
    LumaTextLibraryPath(std::wstring(path));
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
    ReleaseMsgWindow();
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
