#include "fluentui/Button.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace fui {
namespace {

Color Rgba(uint32_t rgb, float alpha) { return Color::Hex(rgb, alpha); }

} // namespace

void Button::RelayoutParent() { Control::RelayoutParent(); }

Size Button::Measure(Size, const Theme& theme) {
    // pad 8 + 图标列 20 + gap 4；最小 32×32
    float width = 16.0f;
    if (!glyph_.empty()) width += 20.0f + (text_.empty() ? 0.0f : 4.0f);
    if (!text_.empty()) width += UiText().MeasureText(text_, TextRole::Body).w;
    return {std::max(width, 32.0f), theme.button_height};
}

void Button::Draw(Painter& painter, const Theme& theme) {
    const Rect& r = absolute_;
    Color fill, border, foreground, edge;
    bool edge_on_top = theme.dark;
    bool draw_edge = false;

    switch (kind_) {
    case ButtonKind::Primary: {
        fill = theme.accent;
        border = fill;
        foreground = theme.primary_text;
        if (!enabled_) {
            fill = theme.dark ? Color::Hex(0x343434) : Color::Hex(0xCDCDCD);
            border = fill;
            foreground = theme.dark ? Rgba(0xFFFFFF, 0.43f) : Rgba(0xFFFFFF, 0.90f);
        } else {
            if (hovered_) fill = theme.accent_hover;
            if (pressed_) fill = theme.accent_pressed;
            border = fill;
            // Primary 高光固定画在底部：暗色用提亮阶，亮色用压暗阶
            edge = theme.dark ? ShiftAccentColor(theme.accent, 1.25f, 0.90f)
                              : ShiftAccentColor(theme.accent, 0.85f, 1.05f);
            draw_edge = true;
            edge_on_top = false;
            if (pressed_) foreground = theme.primary_text_pressed;
        }
        break;
    }
    case ButtonKind::Danger: {
        if (!enabled_) {
            fill = theme.dark ? Color::Hex(0x343434) : Color::Hex(0xCDCDCD);
            border = fill;
            foreground = theme.dark ? Rgba(0xFFFFFF, 0.43f) : Rgba(0xFFFFFF, 0.90f);
        } else {
            fill = theme.danger;
            if (hovered_ || pressed_) fill = theme.danger_hover;
            border = fill;
            edge = fill;
            draw_edge = true;
            edge_on_top = false;
            foreground = Color::Hex(0xFFFFFF);
        }
        break;
    }
    case ButtonKind::Transparent: {
        border = Color{0, 0, 0, 0};
        fill = Color{0, 0, 0, 0};
        foreground = theme.text;
        if (enabled_) {
            if (hovered_) {
                fill = theme.dark ? Rgba(0xFFFFFF, 9.0f / 255.0f) : Rgba(0x000000, 9.0f / 255.0f);
            }
            if (pressed_) {
                fill = theme.dark ? Rgba(0xFFFFFF, 6.0f / 255.0f) : Rgba(0x000000, 6.0f / 255.0f);
                foreground = theme.dark ? Rgba(0xFFFFFF, 0.786f) : Rgba(0x000000, 0.63f);
            }
        } else {
            foreground = theme.text_disabled;
        }
        break;
    }
    case ButtonKind::Standard:
    default: {
        fill = theme.fill_input;
        border = theme.control_stroke;
        foreground = theme.text;
        if (enabled_) {
            if (hovered_) fill = theme.fill_input_hover;
            if (pressed_) {
                fill = theme.fill_input_pressed;
                foreground = theme.dark ? Rgba(0xFFFFFF, 0.786f) : Rgba(0x000000, 0.63f);
            }
            edge = theme.edge_light;
            draw_edge = true;
        } else {
            fill = theme.fill_input_disabled;
            foreground = theme.text_disabled;
        }
        break;
    }
    }

    painter.FillRoundedRect(r, theme.radius_control, fill);
    if (border.a > 0.0f) painter.StrokeRoundedRect(r, theme.radius_control, border);
    if (draw_edge && edge.a > 0.0f) {
        // 1px 边缘高光：暗色主题在顶边（top+0.5），亮色在底边（bottom−0.5）
        const float y = edge_on_top
            ? std::floor(r.y * painter.Scale() + 0.5f) / painter.Scale() + 0.5f
            : std::floor(r.Bottom() * painter.Scale()) / painter.Scale() - 0.5f;
        painter.FillRect({r.x + theme.radius_control, y, r.w - theme.radius_control * 2.0f, 1.0f},
                         edge);
    }
    if (focused_ && enabled_) {
        painter.DrawFocusRing(r, theme.radius_control, theme.accent, theme.focus_ring_width);
    }

    // 内容：有图标时左对齐（pad 8 + 图标列 20 + gap 4），纯文本居中
    const float glyph_w = glyph_.empty() ? 0.0f : 16.0f;
    const float text_w = text_.empty() ? 0.0f : UiText().MeasureText(text_, TextRole::Body).w;
    const float gap = (glyph_w > 0.0f && text_w > 0.0f) ? 4.0f : 0.0f;
    const float content_w = glyph_w + gap + text_w;
    const float left = (glyph_w > 0.0f) ? r.x + 8.0f : r.x + (r.w - content_w) * 0.5f;
    float x = left;
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, {x, r.y, 20.0f, r.h}, 16.0f, foreground);
        x += glyph_w + gap;
    }
    if (!text_.empty()) {
        painter.DrawText(text_, {x, r.y, text_w + 2.0f, r.h}, TextRole::Body, foreground);
    }
}

void Button::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    if (local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
        local.y <= absolute_.h) {
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
    focused_ = focused;
    Invalidate();
}

} // namespace fui
