// lumen/wmain.h — 非 CMake 用户的 wWinMain 入口。与 lumen::main 互斥（否则双定义）。
// Events: 无（本头无订阅事件）
// Keys: 无
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Main.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

#ifndef LUMEN_MAIN
#define LUMEN_MAIN()                                                                 \
    int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {                         \
        int argc = 0;                                                                \
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);                  \
        std::vector<std::wstring_view> args;                                         \
        if (argv && argc > 0) {                                                      \
            args.reserve(static_cast<size_t>(argc));                                 \
            for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);               \
        }                                                                            \
        const int code = lumen_main(args);                                           \
        if (argv) LocalFree(argv);                                                   \
        return code;                                                                 \
    }
#endif
