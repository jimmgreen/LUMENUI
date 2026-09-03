// lumen/Clipboard.h — Unicode 文本 / 文件列表 / BGRA 位图。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lumen::clipboard {

bool Text(std::wstring_view text);
std::wstring Text();

bool Files(std::span<const std::wstring> paths);
std::vector<std::wstring> Files();

// bgra 为 top-down 32bpp，长度至少 w*h*4。
bool Image(std::span<const std::byte> bgra, int width, int height);

} // namespace lumen::clipboard
