#include "fluentui/Theme.h"
#include <windows.h>
#include <dwmapi.h>

namespace fui {
namespace {

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}

float Luminance(Color c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

} // namespace

Theme MakeTheme(bool dark, Color accent) {
    Theme t;
    t.dark = dark;
    t.accent = accent;
    if (dark) {
        t.accent_hover = Mix(accent, Color::Hex(0xFFFFFF), 0.15f);
        t.accent_pressed = Mix(accent, Color::Hex(0x000000), 0.15f);
        t.bg = Color::Hex(0x202020);
        t.card = Color::Hex(0x2B2B2B);
        t.flyout = Color::Hex(0x2C2C2C);
        t.divider = Color{1, 1, 1, 0.0837f};
        t.text = Color::Hex(0xFFFFFF);
        t.text_secondary = Color{1, 1, 1, 0.786f};
        t.text_disabled = Color{1, 1, 1, 0.363f};
        t.control_fill = Color{1, 1, 1, 0.0605f};
        t.control_fill_hover = Color{1, 1, 1, 0.0837f};
        t.control_fill_pressed = Color{1, 1, 1, 0.0326f};
        t.control_stroke = Color{1, 1, 1, 0.0698f};
        t.control_stroke_strong = Color{1, 1, 1, 0.1804f};
        t.focus_ring = Color::Hex(0xFFFFFF);
        t.success = Color::Hex(0x6CCB5F);
        t.selection = Color{accent.r, accent.g, accent.b, 0.16f};
    } else {
        t.accent_hover = Mix(accent, Color::Hex(0x000000), 0.10f);
        t.accent_pressed = Mix(accent, Color::Hex(0x000000), 0.20f);
        t.bg = Color::Hex(0xF3F3F3);
        t.card = Color::Hex(0xFBFBFB);
        t.flyout = Color::Hex(0xFFFFFF);
        t.divider = Color{0, 0, 0, 0.0803f};
        t.text = Color::Hex(0x1A1A1A);
        t.text_secondary = Color{0, 0, 0, 0.606f};
        t.text_disabled = Color{0, 0, 0, 0.363f};
        t.control_fill = Color::Hex(0xFDFDFD);
        t.control_fill_hover = Color::Hex(0xF6F6F6);
        t.control_fill_pressed = Color::Hex(0xF0F0F0);
        t.control_stroke = Color{0, 0, 0, 0.1195f};
        t.control_stroke_strong = Color{0, 0, 0, 0.4457f};
        t.focus_ring = Color::Hex(0x000000);
        t.success = Color::Hex(0x0F7B0F);
        t.selection = Color{accent.r, accent.g, accent.b, 0.14f};
    }
    t.accent_text = Luminance(accent) > 0.55f ? Color::Hex(0x000000) : Color::Hex(0xFFFFFF);
    t.danger = Color::Hex(0xC42B1C);
    return t;
}

Color SystemAccentColor() {
    COLORREF color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)) && opaque) {
        // COLORREF 是 0x00BBGGRR
        return Color::Hex(GetRValue(color) << 16 | GetGValue(color) << 8 | GetBValue(color));
    }
    return Color::Hex(0x0078D4);
}

bool SystemPrefersDark() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
                                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                  L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status == ERROR_SUCCESS) return value == 0;
    return false;
}

} // namespace fui
