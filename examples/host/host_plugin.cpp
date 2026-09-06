#include <windows.h>
#include <lumen/lumen.h>
#include <memory>

namespace {
std::unique_ptr<lumen::Window> window;
DWORD ui_thread = 0;
bool OnUiThread() { return ui_thread == GetCurrentThreadId(); }
}

// All exports run on the host UI thread. DllMain does no UI/COM work.
extern "C" __declspec(dllexport) BOOL HostOpen(HWND owner, BOOL embedded) {
    if (window) {
        if (!OnUiThread()) return FALSE;
        window->Show();
        return TRUE;
    }
    ui_thread = GetCurrentThreadId();
    lumen::App::HostMode(true);
    lumen::WindowSpec spec;
    spec.title = L"LUMEN hosted window";
    spec.size = {420.0f, 280.0f};
    spec.owner = owner;
    spec.matchDpiHwnd = embedded ? owner : nullptr;
    spec.titleBar = !embedded;
    window = std::make_unique<lumen::Window>(spec);
    auto& body = window->Root().Add<lumen::Column>().Padding(16.0f).Spacing(12.0f);
    body.Add<lumen::Label>(embedded ? L"Embedded child" : L"Owned window");
    body.Add<lumen::TextBox>().Placeholder(L"Draft survives hide and reopen");
    body.Add<lumen::Button>(L"Hide").OnClick([] { if (window) window->Hide(); });
    window->OnClosing([] { window->Hide(); return false; });
    if (embedded) {
        const auto child = static_cast<HWND>(window->NativeHandle());
        SetWindowLongPtrW(child, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetLastError(ERROR_SUCCESS);
        if (!SetParent(child, owner) && GetLastError() != ERROR_SUCCESS) { window.reset(); return FALSE; }
        RECT client{};
        GetClientRect(owner, &client);
        SetWindowPos(child, nullptr, 0, 0, client.right, client.bottom,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    window->Show();
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL HostHide() {
    if (!OnUiThread()) return FALSE;
    if (window) window->Hide();
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL HostDestroy() {
    if (!OnUiThread()) return FALSE;
    // Destroy the wrapper before querying CanShutdown. Close may be vetoed.
    window.reset();
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL HostCanUnload() {
    if (!OnUiThread() || window || !lumen::App::CanShutdown()) return FALSE;
    lumen::App::Shutdown();
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL HostResize(int width, int height) {
    if (!OnUiThread() || !window) return FALSE;
    SetWindowPos(static_cast<HWND>(window->NativeHandle()), nullptr, 0, 0, width, height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}
