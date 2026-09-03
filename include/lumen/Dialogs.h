// lumen/Dialogs.h — 系统文件/文件夹对话框（同步模态，不暴露 COM）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumen {

class Window;

namespace dialogs {

// filter 形如 L"文本|*.txt;*.md|全部|*.*"；空则不过滤。
std::optional<std::wstring> PickFile(Window& window, std::wstring_view filter = L"");
std::vector<std::wstring> PickFiles(Window& window, std::wstring_view filter = L"");
std::optional<std::wstring> PickFolder(Window& window);
std::optional<std::wstring> SaveFile(Window& window, std::wstring_view default_name = L"",
                                     std::wstring_view filter = L"");

} // namespace dialogs
} // namespace lumen
