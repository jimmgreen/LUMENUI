#include <windows.h>
#include <cstdio>
#include <string>

bool Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

int wmain() {
    // This executable stands in for the host: it owns DPI and the message pump.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring plugin(path);
    plugin.resize(plugin.find_last_of(L"\\/") + 1);
    plugin += L"lumen_host_plugin.dll";
    HWND owner = CreateWindowExW(0, L"STATIC", L"External host", WS_OVERLAPPEDWINDOW,
        100, 100, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!owner) return 1;
    for (int embedded = 0; embedded != 2; ++embedded) {
        for (int cycle = 0; cycle != 12; ++cycle) {
            const HMODULE module = LoadLibraryW(plugin.c_str());
            if (!module) return 2;
            const auto open = reinterpret_cast<BOOL(*)(HWND, BOOL)>(GetProcAddress(module, "HostOpen"));
            const auto hide = reinterpret_cast<BOOL(*)()>(GetProcAddress(module, "HostHide"));
            const auto destroy = reinterpret_cast<BOOL(*)()>(GetProcAddress(module, "HostDestroy"));
            const auto unload = reinterpret_cast<BOOL(*)()>(GetProcAddress(module, "HostCanUnload"));
            const auto resize = reinterpret_cast<BOOL(*)(int, int)>(GetProcAddress(module, "HostResize"));
            if (!open || !hide || !destroy || !unload || !resize) return 3;
            if (!open(owner, embedded) || !Pump() || unload()) return 4;
            if (!hide() || !Pump() || !open(owner, embedded) || !resize(480, 320) || !Pump()) return 5;
            if (!destroy() || !Pump()) return 6;
            bool ready = false;
            for (int retry = 0; retry < 100 && !ready; ++retry) {
                ready = unload() != FALSE;
                if (!ready) { if (!Pump()) return 7; Sleep(1); }
            }
            if (!ready) return 8; // host refuses unloading while module code/providers remain live
            FreeLibrary(module);
            if (!Pump()) return 9;
        }
        std::printf("%s: 12 DLL load/open/hide/reopen/destroy/unload cycles PASS\n",
            embedded ? "embedded-child" : "owned-window");
    }
    DestroyWindow(owner);
    return 0;
}
