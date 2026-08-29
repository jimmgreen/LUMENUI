#include "fluentui/App.h"
#include <windows.h>

namespace fui {

App::App() {
    // Per-Monitor V2 DPI 感知；旧系统上失败则维持默认（DPI 修正退化为系统级）。
    if (SetProcessDpiAwarenessContext) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

App::~App() = default;

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

} // namespace fui
