#include "lumen/Dialogs.h"
#include "lumen/Window.h"
#include <windows.h>
#include <shobjidl.h>
#include <vector>

namespace lumen {
namespace dialogs {
namespace {

struct ComInit {
    bool owned = false;
    ComInit() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        owned = SUCCEEDED(hr);
        if (hr == S_FALSE) owned = false;   // 已初始化，勿成对 Uninitialize
    }
    ~ComInit() {
        if (owned) CoUninitialize();
    }
};

HWND HwndOf(Window& window) {
    return static_cast<HWND>(window.NativeHandle());
}

void ParseFilter(std::wstring_view filter, std::vector<std::wstring>& names,
                 std::vector<std::wstring>& specs, std::vector<COMDLG_FILTERSPEC>& out) {
    names.clear();
    specs.clear();
    out.clear();
    if (filter.empty()) return;
    std::wstring buf(filter);
    size_t start = 0;
    std::wstring name;
    while (start <= buf.size()) {
        size_t bar = buf.find(L'|', start);
        if (bar == std::wstring::npos) bar = buf.size();
        std::wstring piece = buf.substr(start, bar - start);
        if (name.empty()) {
            name = std::move(piece);
        } else {
            names.push_back(std::move(name));
            specs.push_back(std::move(piece));
            name.clear();
        }
        if (bar == buf.size()) break;
        start = bar + 1;
    }
    out.reserve(names.size());
    for (size_t i = 0; i < names.size() && i < specs.size(); ++i) {
        out.push_back(COMDLG_FILTERSPEC{names[i].c_str(), specs[i].c_str()});
    }
}

std::wstring PathFromItem(IShellItem* item) {
    if (!item) return {};
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return {};
    std::wstring out(path);
    CoTaskMemFree(path);
    return out;
}

void ApplyFilter(IFileDialog* dialog, std::wstring_view filter, std::vector<std::wstring>& names,
                 std::vector<std::wstring>& specs, std::vector<COMDLG_FILTERSPEC>& parsed) {
    ParseFilter(filter, names, specs, parsed);
    if (!parsed.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(parsed.size()), parsed.data());
        dialog->SetFileTypeIndex(1);
    }
}

} // namespace

std::optional<std::wstring> PickFile(Window& window, std::wstring_view filter) {
    ComInit com;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))) ||
        !dialog) {
        return std::nullopt;
    }
    std::vector<std::wstring> names, specs;
    std::vector<COMDLG_FILTERSPEC> parsed;
    ApplyFilter(dialog, filter, names, specs, parsed);
    std::optional<std::wstring> result;
    if (SUCCEEDED(dialog->Show(HwndOf(window)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            result = PathFromItem(item);
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::vector<std::wstring> PickFiles(Window& window, std::wstring_view filter) {
    ComInit com;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))) ||
        !dialog) {
        return {};
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT);
    std::vector<std::wstring> names, specs;
    std::vector<COMDLG_FILTERSPEC> parsed;
    ApplyFilter(dialog, filter, names, specs, parsed);
    std::vector<std::wstring> result;
    if (SUCCEEDED(dialog->Show(HwndOf(window)))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)) && items) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
                    std::wstring path = PathFromItem(item);
                    if (!path.empty()) result.push_back(std::move(path));
                    item->Release();
                }
            }
            items->Release();
        }
    }
    dialog->Release();
    return result;
}

std::optional<std::wstring> PickFolder(Window& window) {
    ComInit com;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))) ||
        !dialog) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS);
    std::optional<std::wstring> result;
    if (SUCCEEDED(dialog->Show(HwndOf(window)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            result = PathFromItem(item);
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::optional<std::wstring> SaveFile(Window& window, std::wstring_view default_name,
                                     std::wstring_view filter) {
    ComInit com;
    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))) ||
        !dialog) {
        return std::nullopt;
    }
    if (!default_name.empty()) {
        dialog->SetFileName(std::wstring(default_name).c_str());
    }
    std::vector<std::wstring> names, specs;
    std::vector<COMDLG_FILTERSPEC> parsed;
    ApplyFilter(dialog, filter, names, specs, parsed);
    std::optional<std::wstring> result;
    if (SUCCEEDED(dialog->Show(HwndOf(window)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            result = PathFromItem(item);
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

} // namespace dialogs
} // namespace lumen
