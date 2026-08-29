#include "fluentui/Button.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace fui {
namespace {

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}

} // namespace

void Button::RelayoutParent() { Control::RelayoutParent(); }

Size Button::Measure(Size, const Theme& theme) {
    float width = 22.0f;
    if (!glyph_.empty()) width += 16.0f + (text_.empty() ? 0.0f : 8.0f);
    if (!text_.empty()) width += UiText().MeasureText(text_, TextRole::Body).w;
    return {std::max(width, 64.0f), theme.button_height};
}

void Button::Draw(Painter& painter, const Theme& theme) {
    Color fill = theme.control_fill;
    Color border = theme.control_stroke;
    Color foreground = theme.text;
    const float hover = hover_t_, press = press_t_;

    switch (kind_) {
    case ButtonKind::Primary:
        fill = Mix(theme.accent, theme.accent_hover, hover);
        fill = Mix(fill, theme.accent_pressed, press);
        foreground = theme.accent_text;
        border = Color{0, 0, 0, 0};
        break;
    case ButtonKind::Transparent:
        fill = Mix(Color{0, 0, 0, 0}, theme.control_fill_hover, hover);
        fill = Mix(fill, theme.control_fill_pressed, press);
        border = Color{0, 0, 0, 0};
        break;
    case ButtonKind::Danger:
        fill = Mix(theme.danger, Mix(theme.danger, Color::Hex(0xFFFFFF), 0.12f), hover);
        fill = Mix(fill, Mix(theme.danger, Color::Hex(0x000000), 0.20f), press);
        foreground = Color::Hex(0xFFFFFF);
        border = Color{0, 0, 0, 0};
        break;
    case ButtonKind::Standard:
    default:
        fill = Mix(theme.control_fill, theme.control_fill_hover, hover);
        fill = Mix(fill, theme.control_fill_pressed, press);
        break;
    }
    if (!enabled_) {
        fill.a *= 0.45f;
        foreground = theme.text_disabled;
        border.a *= 0.5f;
    }

    painter.FillRoundedRect(absolute_, theme.radius_control, fill);
    if (border.a > 0.0f) painter.StrokeRoundedRect(absolute_, theme.radius_control, border);
    if (focused_ && enabled_) {
        painter.DrawFocusRing(absolute_, theme.radius_control, theme.focus_ring);
    }

    // 内容水平居中：字形 + 间距 + 文本
    const float glyph_w = glyph_.empty() ? 0.0f : 16.0f;
    const float text_w = text_.empty() ? 0.0f : UiText().MeasureText(text_, TextRole::Body).w;
    const float gap = (glyph_w > 0.0f && text_w > 0.0f) ? 8.0f : 0.0f;
    const float content_w = glyph_w + gap + text_w;
    float x = absolute_.x + (absolute_.w - content_w) * 0.5f;
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, {x, absolute_.y, 16.0f, absolute_.h}, 16.0f, foreground);
        x += glyph_w + gap;
    }
    if (!text_.empty()) {
        painter.DrawText(text_, {x, absolute_.y, text_w + 2.0f, absolute_.h}, TextRole::Body,
                         foreground);
    }
}

void Button::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    if (local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w && local.y <= absolute_.h) {
        if (click_) click_();
    }
}

bool Button::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE || vk == VK_RETURN) {
        if (click_) click_();
        return true;
    }
    return false;
}

void Button::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

} // namespace fui
