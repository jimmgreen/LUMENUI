// hotkey.h — Ctrl+S 和弦解析（HotkeyBox / 窗口加速键 / 全局热键共用）。
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace lumen {

bool ParseChord(std::wstring_view text, uint32_t& vk, bool& ctrl, bool& shift, bool& alt);
std::wstring FormatChord(uint32_t vk, bool ctrl, bool shift, bool alt);
// 当前修饰键状态是否与和弦匹配（vk 为本次 WM_KEYDOWN 的虚拟键）。
bool ChordMatches(std::wstring_view text, uint32_t vk);

} // namespace lumen
