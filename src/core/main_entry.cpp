#include "lumen/Main.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring_view> args;
    if (argv && argc > 0) {
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    }
    const int code = lumen_main(args);
    if (argv) LocalFree(argv);
    return code;
}
