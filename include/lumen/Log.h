// lumen/Log.h — 公共诊断日志。绘制热路径不要调用。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

namespace lumen {

enum class LogLevel : uint8_t { Debug, Info, Warn, Error };

void LogEnable(const wchar_t* path);
void Log(const wchar_t* fmt, ...);
void Log(LogLevel level, const wchar_t* fmt, ...);
bool LogEnabled() noexcept;
void SetLogSink(std::function<void(LogLevel, std::wstring_view)> sink);
void DebugWrite(std::wstring_view text);

} // namespace lumen
