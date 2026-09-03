#include "hotkey.h"
#include <windows.h>
#include <cwctype>
#include <string>

namespace lumen {
namespace {

std::wstring KeyName(uint32_t vk) {
    if (vk >= 'A' && vk <= 'Z') return {static_cast<wchar_t>(vk)};
    if (vk >= '0' && vk <= '9') return {static_cast<wchar_t>(vk)};
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return L"Num" + std::to_wstring(vk - VK_NUMPAD0);
    }
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    switch (vk) {
    case VK_RETURN: return L"Enter";
    case VK_SPACE: return L"Space";
    case VK_ESCAPE: return L"Esc";
    case VK_TAB: return L"Tab";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_OEM_PLUS: return L"Plus";
    case VK_OEM_MINUS: return L"Minus";
    case VK_OEM_COMMA: return L"Comma";
    case VK_OEM_PERIOD: return L"Period";
    case VK_PAUSE: return L"Pause";
    case VK_SNAPSHOT: return L"PrintScreen";
    default: return L"#" + std::to_wstring(vk);
    }
}

bool ParseKey(std::wstring_view key, uint32_t& vk) {
    if (key.empty()) return false;
    if (key.size() == 1) {
        const wchar_t ch = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(key[0])));
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) {
            vk = static_cast<uint32_t>(ch);
            return true;
        }
        return false;
    }
    auto eq = [&](const wchar_t* name) {
        if (key.size() != std::char_traits<wchar_t>::length(name)) return false;
        for (size_t i = 0; i < key.size(); ++i) {
            if (std::towupper(static_cast<wint_t>(key[i])) !=
                std::towupper(static_cast<wint_t>(name[i]))) {
                return false;
            }
        }
        return true;
    };
    if ((key[0] == L'F' || key[0] == L'f') && key.size() <= 3) {
        int n = 0;
        for (size_t i = 1; i < key.size(); ++i) {
            if (key[i] < L'0' || key[i] > L'9') return false;
            n = n * 10 + (key[i] - L'0');
        }
        if (n >= 1 && n <= 24) {
            vk = static_cast<uint32_t>(VK_F1 + n - 1);
            return true;
        }
    }
    if (eq(L"Enter") || eq(L"Return")) { vk = VK_RETURN; return true; }
    if (eq(L"Space")) { vk = VK_SPACE; return true; }
    if (eq(L"Esc") || eq(L"Escape")) { vk = VK_ESCAPE; return true; }
    if (eq(L"Tab")) { vk = VK_TAB; return true; }
    if (eq(L"Backspace")) { vk = VK_BACK; return true; }
    if (eq(L"Delete") || eq(L"Del")) { vk = VK_DELETE; return true; }
    if (eq(L"Insert") || eq(L"Ins")) { vk = VK_INSERT; return true; }
    if (eq(L"Home")) { vk = VK_HOME; return true; }
    if (eq(L"End")) { vk = VK_END; return true; }
    if (eq(L"PageUp") || eq(L"PgUp")) { vk = VK_PRIOR; return true; }
    if (eq(L"PageDown") || eq(L"PgDn")) { vk = VK_NEXT; return true; }
    if (eq(L"Left")) { vk = VK_LEFT; return true; }
    if (eq(L"Right")) { vk = VK_RIGHT; return true; }
    if (eq(L"Up")) { vk = VK_UP; return true; }
    if (eq(L"Down")) { vk = VK_DOWN; return true; }
    if (eq(L"Plus")) { vk = VK_OEM_PLUS; return true; }
    if (eq(L"Minus")) { vk = VK_OEM_MINUS; return true; }
    if (eq(L"Comma")) { vk = VK_OEM_COMMA; return true; }
    if (eq(L"Period")) { vk = VK_OEM_PERIOD; return true; }
    if (eq(L"Pause")) { vk = VK_PAUSE; return true; }
    if (eq(L"PrintScreen")) { vk = VK_SNAPSHOT; return true; }
    if (key.size() == 4 && (key[0] == L'N' || key[0] == L'n') &&
        (key[1] == L'u' || key[1] == L'U') && (key[2] == L'm' || key[2] == L'M') &&
        key[3] >= L'0' && key[3] <= L'9') {
        vk = static_cast<uint32_t>(VK_NUMPAD0 + (key[3] - L'0'));
        return true;
    }
    return false;
}

} // namespace

bool ParseChord(std::wstring_view text, uint32_t& vk, bool& ctrl, bool& shift, bool& alt) {
    vk = 0;
    ctrl = shift = alt = false;
    if (text.empty()) return true;
    std::wstring key;
    size_t i = 0;
    while (i < text.size()) {
        if (text.size() - i >= 5 &&
            (text.substr(i, 5) == L"Ctrl+" || text.substr(i, 5) == L"CTRL+" ||
             text.substr(i, 5) == L"ctrl+")) {
            ctrl = true;
            i += 5;
            continue;
        }
        if (text.size() - i >= 6 &&
            (text.substr(i, 6) == L"Shift+" || text.substr(i, 6) == L"SHIFT+" ||
             text.substr(i, 6) == L"shift+")) {
            shift = true;
            i += 6;
            continue;
        }
        if (text.size() - i >= 4 &&
            (text.substr(i, 4) == L"Alt+" || text.substr(i, 4) == L"ALT+" ||
             text.substr(i, 4) == L"alt+")) {
            alt = true;
            i += 4;
            continue;
        }
        key.assign(text.substr(i));
        break;
    }
    return ParseKey(key, vk);
}

std::wstring FormatChord(uint32_t vk, bool ctrl, bool shift, bool alt) {
    if (vk == 0) return {};
    std::wstring out;
    if (ctrl) out += L"Ctrl+";
    if (shift) out += L"Shift+";
    if (alt) out += L"Alt+";
    out += KeyName(vk);
    return out;
}

bool ChordMatches(std::wstring_view text, uint32_t vk) {
    uint32_t want = 0;
    bool ctrl = false, shift = false, alt = false;
    if (!ParseChord(text, want, ctrl, shift, alt) || want == 0) return false;
    if (want != vk) return false;
    const bool have_ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool have_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool have_alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return have_ctrl == ctrl && have_shift == shift && have_alt == alt;
}

} // namespace lumen
