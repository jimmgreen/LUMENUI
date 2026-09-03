// lumen/Text.h — UTF-8 / UTF-16 转换与字面量。Core.h 只留几何与颜色。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include <string>
#include <string_view>

namespace lumen {

inline std::string Utf8(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());
    for (size_t i = 0; i < wide.size(); ++i) {
        const char32_t ch = static_cast<unsigned short>(wide[i]);
        char32_t cp = ch;
        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < wide.size()) {
            const char32_t lo = static_cast<unsigned short>(wide[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline std::wstring U8(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        size_t n = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = static_cast<char32_t>(((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F));
            n = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = static_cast<char32_t>(((c & 0x0F) << 12) |
                                       ((static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6) |
                                       (static_cast<unsigned char>(utf8[i + 2]) & 0x3F));
            n = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = static_cast<char32_t>(((c & 0x07) << 18) |
                                       ((static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 12) |
                                       ((static_cast<unsigned char>(utf8[i + 2]) & 0x3F) << 6) |
                                       (static_cast<unsigned char>(utf8[i + 3]) & 0x3F));
            n = 4;
        } else {
            cp = 0xFFFD;
        }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
        i += n;
    }
    return out;
}

inline std::wstring operator""_w(const char8_t* s, size_t n) {
    return U8(std::string_view(reinterpret_cast<const char*>(s), n));
}

} // namespace lumen
