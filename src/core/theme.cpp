#include "fluentui/Theme.h"
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>

namespace fui {
namespace {

Color Rgba(uint32_t rgb, float alpha) {
    return Color::Hex(rgb, alpha);
}

float Luminance(Color c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// HSV 明度/饱和度派生 accent 色阶（对齐 WinUI 的 AccentShades 做法）。
void ShiftAccent(Color c, float value_scale, float saturation_scale, Color& out) {
    const float r = c.r, g = c.g, b = c.b;
    const float max_c = std::max({r, g, b});
    const float min_c = std::min({r, g, b});
    const float delta = max_c - min_c;
    float hue = 0.0f;
    if (delta > 0.0f) {
        if (max_c == r) hue = std::fmod((g - b) / delta, 6.0f);
        else if (max_c == g) hue = (b - r) / delta + 2.0f;
        else hue = (r - g) / delta + 4.0f;
        hue *= 60.0f;
        if (hue < 0.0f) hue += 360.0f;
    }
    float saturation = max_c <= 0.0f ? 0.0f : delta / max_c;
    float value = max_c;
    saturation = Clamp(saturation * saturation_scale, 0.0f, 1.0f);
    value = Clamp(value * value_scale, 0.0f, 1.0f);

    const float chroma = value * saturation;
    const float hp = hue / 60.0f;
    const float x = chroma * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float rr = 0, gg = 0, bb = 0;
    if (hp < 1.0f) { rr = chroma; gg = x; }
    else if (hp < 2.0f) { rr = x; gg = chroma; }
    else if (hp < 3.0f) { gg = chroma; bb = x; }
    else if (hp < 4.0f) { gg = x; bb = chroma; }
    else if (hp < 5.0f) { rr = x; bb = chroma; }
    else { rr = chroma; bb = x; }
    const float m = value - chroma;
    out = {rr + m, gg + m, bb + m, c.a};
}

} // namespace

Color ShiftAccentColor(Color accent, float value_scale, float saturation_scale) {
    Color out;
    ShiftAccent(accent, value_scale, saturation_scale, out);
    return out;
}

Theme MakeTheme(bool dark, Color accent) {
    Theme t;
    t.dark = dark;
    if (dark) {
        // 暗色 accent 色阶：hover 提亮 0.85、按下压暗 0.70
        ShiftAccent(accent, 0.85f, 1.05f, t.accent_hover);
        ShiftAccent(accent, 0.70f, 1.02f, t.accent_pressed);
        t.bg = Color::Hex(0x1A1A1A);
        t.text = Color::Hex(0xFFFFFF);
        t.text_secondary = Rgba(0xFFFFFF, 0.6063f);
        t.text_disabled = Rgba(0xFFFFFF, 0.40f);
        t.fill_hover = Rgba(0xFFFFFF, 0.08f);
        t.fill_pressed = Rgba(0xFFFFFF, 0.06f);
        t.fill_selected = Color{accent.r, accent.g, accent.b, 0.15f};
        t.fill_input = Rgba(0xFFFFFF, 0.0605f);
        t.fill_input_hover = Rgba(0xFFFFFF, 0.0837f);
        t.fill_input_pressed = Rgba(0xFFFFFF, 0.0326f);
        t.fill_input_focus = Rgba(0x1E1E1E, 0.70f);
        t.fill_input_disabled = Rgba(0xFFFFFF, 0.0419f);
        t.stroke_card = Rgba(0xFFFFFF, 0.045f);
        t.stroke_divider = Rgba(0xFFFFFF, 0.06f);
        t.control_stroke = Rgba(0xFFFFFF, 0.053f);
        t.stroke_input_bottom = Rgba(0xFFFFFF, 0.5442f);
        t.edge_light = Rgba(0xFFFFFF, 0.08f);
        t.primary_text = Color::Hex(0x000000);
        t.primary_text_pressed = Rgba(0x000000, 0.63f);
        t.surface_flyout = Color::Hex(0x2B2B2B);
        t.scrollbar_thumb = Rgba(0xFFFFFF, 139.0f / 255.0f);
        t.danger = Color::Hex(0xC42B1C);
        t.danger_hover = Rgba(0xC42B1C, 0.85f);
        t.success = Color::Hex(0x6CCB5F);
    } else {
        // 亮色 accent 色阶：hover 提亮 1.12、按下加深 1.40
        ShiftAccent(accent, 1.12f, 0.95f, t.accent_hover);
        ShiftAccent(accent, 1.40f, 0.85f, t.accent_pressed);
        t.bg = Color::Hex(0xF5F5F5);
        t.text = Color::Hex(0x1A1A1A);
        t.text_secondary = Rgba(0x000000, 0.6063f);
        t.text_disabled = Rgba(0x000000, 0.36f);
        t.fill_hover = Rgba(0x000000, 0.05f);
        t.fill_pressed = Rgba(0x000000, 0.03f);
        t.fill_selected = Color{accent.r, accent.g, accent.b, 0.12f};
        t.fill_input = Rgba(0xFFFFFF, 0.70f);
        t.fill_input_hover = Rgba(0xF9F9F9, 0.50f);
        t.fill_input_pressed = Rgba(0xF9F9F9, 0.30f);
        t.fill_input_focus = Color::Hex(0xFFFFFF);
        t.fill_input_disabled = Rgba(0xF9F9F9, 0.30f);
        t.stroke_card = Rgba(0x000000, 0.028f);
        t.stroke_divider = Rgba(0x000000, 0.045f);
        t.control_stroke = Rgba(0x000000, 0.073f);
        t.stroke_input_bottom = Rgba(0x000000, 0.392f);
        t.edge_light = Rgba(0x000000, 0.183f);
        t.primary_text = Color::Hex(0xFFFFFF);
        t.primary_text_pressed = Rgba(0xFFFFFF, 0.63f);
        t.surface_flyout = Color::Hex(0xFBFBFB);
        t.scrollbar_thumb = Rgba(0x000000, 114.0f / 255.0f);
        t.danger = Color::Hex(0xC42B1C);
        t.danger_hover = Rgba(0xC42B1C, 0.85f);
        t.success = Color::Hex(0x0F7B0F);
    }
    t.accent = accent;
    t.accent_text = Luminance(accent) < 0.5f ? Color::Hex(0xFFFFFF) : Color::Hex(0x000000);
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
