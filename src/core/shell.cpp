#include "lumen/Shell.h"
#include <windows.h>
#include <shellapi.h>
#include <string>

namespace lumen::shell {
namespace {

bool Exec(std::wstring_view file, std::wstring_view params, std::wstring_view verb) {
    if (file.empty()) return false;
    std::wstring path(file);
    std::wstring extra(params);
    std::wstring action(verb);
    const INT_PTR rc = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, action.empty() ? nullptr : action.c_str(), path.c_str(),
        extra.empty() ? nullptr : extra.c_str(), nullptr, SW_SHOWNORMAL));
    return rc > 32;
}

} // namespace

bool OpenUrl(std::wstring_view url) { return Exec(url, {}, L"open"); }

bool OpenWithDefault(std::wstring_view path) { return Exec(path, {}, L"open"); }

bool RevealInExplorer(std::wstring_view path) {
    if (path.empty()) return false;
    std::wstring arg = L"/select,\"";
    arg.append(path);
    arg.push_back(L'"');
    return Exec(L"explorer.exe", arg, L"open");
}

} // namespace lumen::shell
