#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cwchar>
#include <functional>
#include <string>
#include <string_view>

namespace lumen {
namespace {

bool g_enabled = false;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::function<void(LogLevel, std::wstring_view)> g_sink;

void WriteUtf8(const wchar_t* line) {
    char utf8[2560];
    const int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (n <= 1) return;
    OutputDebugStringW(line);
    if (g_file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_file, utf8, static_cast<DWORD>(n - 1), &written, nullptr);
}

} // namespace

void DebugWrite(std::wstring_view text) {
    if (text.empty()) return;
    if (text.back() == L'\n') {
        OutputDebugStringW(std::wstring(text).c_str());
        return;
    }
    std::wstring line(text);
    line.push_back(L'\n');
    OutputDebugStringW(line.c_str());
}

void SetLogSink(std::function<void(LogLevel, std::wstring_view)> sink) {
    g_sink = std::move(sink);
}

void LogEnable(const wchar_t* path) {
    wchar_t env[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"LUMEN_LOG", env, MAX_PATH);
    if (n == 1 && env[0] == L'0') {
        g_enabled = false;
        return;
    }
    const wchar_t* file = path;
    if (n > 1 || (n == 1 && env[0] != L'1')) file = env;
    if (!file || !file[0]) {
        g_enabled = false;
        return;
    }
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_file = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    g_enabled = true;
    WriteUtf8(L"---- lumen log ----\n");
}

bool LogEnabled() noexcept { return g_enabled; }

namespace {
const wchar_t* LevelTag(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug: return L"DBG";
    case LogLevel::Info: return L"INF";
    case LogLevel::Warn: return L"WRN";
    case LogLevel::Error: return L"ERR";
    }
    return L"INF";
}

void EmitLog(LogLevel level, const wchar_t* body) {
    if (!body) return;
    const bool always = (level == LogLevel::Warn || level == LogLevel::Error);
    if (g_sink) g_sink(level, body);
    if (!g_enabled && !always) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t line[1200];
    swprintf_s(line, L"%02u:%02u:%02u.%03u [%s] %s\n", st.wHour, st.wMinute, st.wSecond,
               st.wMilliseconds, LevelTag(level), body);
    if (always || g_enabled) OutputDebugStringW(line);
    if (g_enabled && g_file != INVALID_HANDLE_VALUE) {
        char utf8[2560];
        const int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (n > 1) {
            DWORD written = 0;
            WriteFile(g_file, utf8, static_cast<DWORD>(n - 1), &written, nullptr);
        }
    }
}
} // namespace

void Log(const wchar_t* fmt, ...) {
    if (!g_enabled && !g_sink) return;
    if (!fmt) return;
    wchar_t body[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(body, _TRUNCATE, fmt, args);
    va_end(args);
    EmitLog(LogLevel::Info, body);
}

void Log(LogLevel level, const wchar_t* fmt, ...) {
    if (!fmt) return;
    const bool always = (level == LogLevel::Warn || level == LogLevel::Error);
    if (!g_enabled && !g_sink && !always) return;
    wchar_t body[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(body, _TRUNCATE, fmt, args);
    va_end(args);
    EmitLog(level, body);
}

} // namespace lumen
