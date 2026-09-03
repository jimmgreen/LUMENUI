// lumen/Shell.h — 打开 URL / 资源管理器 / 默认程序（ShellExecute 包一层）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <string_view>

namespace lumen::shell {

bool OpenUrl(std::wstring_view url);
bool RevealInExplorer(std::wstring_view path);
bool OpenWithDefault(std::wstring_view path);

} // namespace lumen::shell
