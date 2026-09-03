// lumen/Main.h — 链 lumen::main 时由库提供 wWinMain，用户实现 lumen_main。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <span>
#include <string_view>

int lumen_main(std::span<const std::wstring_view> args);
