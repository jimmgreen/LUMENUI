#include "lumen/Theme.h"
#include <algorithm>
#include <windows.h>

namespace lumen {
namespace {

Color Rgba(uint32_t rgb, float alpha) {
    return Color::Hex(rgb, alpha);
}

// 辉光 token 统一为白色系，glow_intensity 只缩放 alpha。
Color Glow(float alpha, float intensity) {
    return Color{1.0f, 1.0f, 1.0f, alpha * intensity};
}

} // namespace

Theme MakeTheme(float glow_intensity) {
    const float i = Clamp(glow_intensity, 0.0f, 1.0f);
    Theme t;
    t.glow_intensity = i;

    // 阶梯：void 黑 -> carbon -> surface -> surface-light，拉大发光体与暗面的对比
    t.bg = Color::Hex(0x000000);
    t.text = Color::Hex(0xF4F4F5);
    t.text_secondary = Rgba(0xFFFFFF, 0.60f);
    t.text_disabled = Rgba(0xFFFFFF, 0.35f);

    t.fill_hover = Rgba(0xFFFFFF, 0.06f);
    t.fill_pressed = Rgba(0xFFFFFF, 0.10f);
    t.fill_selected = Rgba(0xFFFFFF, 0.12f);
    t.fill_input = Color::Hex(0x050505);
    t.fill_input_hover = Color::Hex(0x0A0A0C);
    t.fill_input_pressed = Color::Hex(0x111010);
    t.fill_input_focus = Color::Hex(0x050505);
    t.fill_input_disabled = Rgba(0xFFFFFF, 0.03f);

    t.stroke_card = Rgba(0xFFFFFF, 0.10f);
    t.stroke_divider = Rgba(0xFFFFFF, 0.06f);
    t.control_stroke = Rgba(0xFFFFFF, 0.20f);
    t.stroke_input_bottom = Rgba(0xFFFFFF, 0.20f);
    t.edge_light = Glow(0.20f, i);

    t.glow_sm = Glow(0.35f, i);
    t.glow_md = Glow(0.45f, i);
    t.glow_lg = Glow(0.65f, i);
    t.spotlight_fill = Glow(0.12f, i);
    t.spotlight_border = Glow(0.50f, i);
    t.specular_line = Glow(0.60f, i);
    t.ambient_flare = Glow(0.08f, i);
    t.grid_line = Rgba(0xFFFFFF, 0.03f);

    t.accent = Color::Hex(0xFFFFFF);
    t.accent_hover = Color::Hex(0xE4E4E7);
    t.accent_pressed = Color::Hex(0xD4D4D8);
    t.accent_text = Color::Hex(0x000000);
    t.primary_text = Color::Hex(0x000000);
    t.primary_text_pressed = Rgba(0x000000, 0.63f);

    t.surface_flyout = Color::Hex(0x121215);
    t.scrollbar_thumb = Color::Hex(0x27272A);
    t.scrollbar_thumb_hover = Color::Hex(0x52525B);
    t.danger = Color::Hex(0xFAFAFA);
    t.danger_hover = Color::Hex(0xFFFFFF);
    t.success = Color::Hex(0xA1A1AA);

    t.duration_fast = 0.12f;
    t.duration_normal = 0.24f;
    t.duration_slow = 0.40f;
    t.ease_standard = Ease::Material;
    t.ease_enter = Ease::CssEaseOut;
    t.ease_exit = Ease::CssEaseIn;
    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    t.motion_scale = anim ? 1.0f : 0.0f;
    t.elevation_spread[0] = 0.0f;
    t.elevation_spread[1] = 0.70f;
    t.elevation_spread[2] = 1.15f;
    t.elevation_spread[3] = 1.55f;
    t.elevation_glow[0] = 0.0f;
    t.elevation_glow[1] = 0.35f;
    t.elevation_glow[2] = 0.55f;
    t.elevation_glow[3] = 0.75f;
    t.elevation_specular[0] = 0.0f;
    t.elevation_specular[1] = 0.35f;
    t.elevation_specular[2] = 0.55f;
    t.elevation_specular[3] = 0.80f;
    return t;
}

Theme ApplyThemeOverride(const Theme& base, const ThemeOverride& o) noexcept {
    Theme t = base;
    float scale = 1.0f;
    switch (o.density) {
    case Density::Comfortable: scale = 1.15f; break;
    case Density::Compact: scale = 0.85f; break;
    case Density::Normal: scale = 1.0f; break;
    case Density::Inherit:
    default: break;
    }
    if (o.density != Density::Inherit) {
        t.button_height *= scale;
        t.input_height *= scale;
        t.menu_item_height *= scale;
        t.list_row_height *= scale;
        t.space_sm *= scale;
        t.space_md *= scale;
        t.space_lg *= scale;
        t.space_xl *= scale;
    }
    auto apply = [](float& dst, float v) {
        if (v >= 0.0f) dst = v;
    };
    apply(t.radius_control, o.radius_control);
    apply(t.radius_card, o.radius_card);
    apply(t.button_height, o.button_height);
    apply(t.input_height, o.input_height);
    apply(t.list_row_height, o.list_row_height);
    apply(t.space_sm, o.space_sm);
    apply(t.space_md, o.space_md);
    apply(t.space_lg, o.space_lg);
    return t;
}

} // namespace lumen
