// app_host.h — App 与 WindowImpl 之间的进程级通道（单实例 / 全局热键）。
#pragma once
#include <windows.h>

namespace lumen {

void AppBindWindow(HWND hwnd);
void AppUnbindWindow(HWND hwnd);
void AppActivateExisting();
bool AppHandleHotkey(WPARAM id);
UINT AppActivateMsg();

} // namespace lumen
