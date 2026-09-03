#include "lumen/Clipboard.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <string>
#include <vector>

namespace lumen::clipboard {
namespace {

struct ClipboardGuard {
    bool ok = false;
    ClipboardGuard() : ok(OpenClipboard(nullptr) != FALSE) {}
    ~ClipboardGuard() {
        if (ok) CloseClipboard();
    }
    explicit operator bool() const noexcept { return ok; }
};

} // namespace

bool Text(std::wstring_view text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return false;
    }
    if (void* data = GlobalLock(mem)) {
        if (!text.empty()) {
            std::memcpy(data, text.data(), text.size() * sizeof(wchar_t));
        }
        static_cast<wchar_t*>(data)[text.size()] = 0;
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
        CloseClipboard();
        return true;
    }
    GlobalFree(mem);
    CloseClipboard();
    return false;
}

std::wstring Text() {
    if (!OpenClipboard(nullptr)) return {};
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    std::wstring out;
    if (handle) {
        if (const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle))) {
            out = data;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return out;
}

bool Files(std::span<const std::wstring> paths) {
    ClipboardGuard clip;
    if (!clip) return false;
    EmptyClipboard();
    size_t chars = 1;
    for (const std::wstring& path : paths) chars += path.size() + 1;
    const size_t bytes = sizeof(DROPFILES) + chars * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!mem) return false;
    auto* drop = static_cast<DROPFILES*>(GlobalLock(mem));
    if (!drop) {
        GlobalFree(mem);
        return false;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    wchar_t* dest = reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(drop) + sizeof(DROPFILES));
    for (const std::wstring& path : paths) {
        if (!path.empty()) {
            std::memcpy(dest, path.data(), path.size() * sizeof(wchar_t));
        }
        dest += path.size();
        *dest++ = L'\0';
    }
    *dest = L'\0';
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_HDROP, mem)) {
        GlobalFree(mem);
        return false;
    }
    return true;
}

std::vector<std::wstring> Files() {
    ClipboardGuard clip;
    if (!clip) return {};
    HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (!drop) return {};
    const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> out;
    out.reserve(n);
    for (UINT i = 0; i < n; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(len, L'\0');
        if (len == 0) continue;
        DragQueryFileW(drop, i, path.data(), len + 1);
        out.push_back(std::move(path));
    }
    return out;
}

bool Image(std::span<const std::byte> bgra, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (bgra.size() < pixels) return false;
    ClipboardGuard clip;
    if (!clip) return false;
    EmptyClipboard();
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = width;
    header.bV5Height = -height;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(pixels);
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    header.bV5CSType = LCS_sRGB;
    const size_t bytes = sizeof(BITMAPV5HEADER) + pixels;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    void* data = GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        return false;
    }
    std::memcpy(data, &header, sizeof(header));
    std::memcpy(static_cast<std::byte*>(data) + sizeof(header), bgra.data(), pixels);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_DIBV5, mem)) {
        GlobalFree(mem);
        return false;
    }
    return true;
}

} // namespace lumen::clipboard
