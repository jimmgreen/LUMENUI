#include "lumen/DropDownButton.h"
#include "lumen/Icons.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace lumen {
namespace {

struct Metrics {
    float height;
    float pad_x;
};

Metrics SizeMetrics(ButtonSize size) {
    switch (size) {
    case ButtonSize::Small: return {40.0f, 12.0f};
    case ButtonSize::Large: return {44.0f, 24.0f};
    default: return {44.0f, 24.0f};
    }
}

TextRole ButtonTextRole(ButtonSize size, ButtonKind kind) {
    if (size == ButtonSize::Small) return TextRole::Caption;
    if (kind == ButtonKind::Primary || kind == ButtonKind::Danger) return TextRole::BodyStrong;
    return TextRole::Body;
}

constexpr float kChevron = 10.0f;
constexpr float kChevronGap = 8.0f;

} // namespace

void DropDownButton::RelayoutParent() { Control::RelayoutParent(); }

void DropDownButton::Open() {
    if (!enabled_) return;
    dropdown_.Emit();
}

Size DropDownButton::Measure(Size, const Theme&) {
    const Metrics m = SizeMetrics(size_);
    const TextRole role = ButtonTextRole(size_, kind_);
    float width = m.pad_x * 2.0f + kChevronGap + kChevron;
    if (!glyph_.empty()) width += 16.0f + 6.0f;
    if (!text_.empty()) width += MeasureText(text_, role).w;
    return {std::max(width, m.height), m.height};
}

void DropDownButton::OnMouseEnter() {
    Control::OnMouseEnter();
    Animate();
}

void DropDownButton::OnMouseLeave() {
    Control::OnMouseLeave();
    Animate();
}

void DropDownButton::OnMouseDown(Point, uint32_t) {
    if (!enabled_) return;
    pressed_ = true;
    Animate();
}

void DropDownButton::OnMouseUp(Point local, uint32_t) {
    const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
                        local.y <= absolute_.h;
    const bool was = pressed_;
    if (pressed_) {
        pressed_ = false;
        Animate();
    }
    if (was && enabled_ && inside) Open();
}

bool DropDownButton::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE || vk == VK_RETURN || vk == VK_DOWN) {
        Open();
        return true;
    }
    return false;
}

void DropDownButton::OnFocusChanged(bool focused) {
    focused_ = focused;
    Animate();
    Invalidate();
}

bool DropDownButton::OnAnimate(float dt) {
    const bool lit = (hovered_ || focused_) && enabled_;
    bool active = Control::OnAnimate(dt);
    active |= EaseTo(glow_t_, lit ? 1.0f : 0.0f, dt, 14.0f);
    active |= EaseTo(scale_t_, pressed_ && enabled_ ? 1.0f : 0.0f, dt, 20.0f);
    return active;
}

void DropDownButton::Draw(Painter& painter, const Theme& theme) {
    const TextRole role = ButtonTextRole(size_, kind_);
    const bool solid = kind_ == ButtonKind::Primary || kind_ == ButtonKind::Danger;
    const Metrics m = SizeMetrics(size_);

    const float shrink = 0.02f * scale_t_;
    const Rect r = scale_t_ > 0.001f
                       ? absolute_.Inset(absolute_.w * shrink * 0.5f, absolute_.h * shrink * 0.5f)
                       : absolute_;
    const float radius = theme.radius_control;

    Color fill{0, 0, 0, 0};
    Color border{0, 0, 0, 0};
    Color foreground = theme.text;
    float rest_glow = 0.0f;
    float hover_glow = 0.0f;
    float glow_spread = 1.0f;

    switch (kind_) {
    case ButtonKind::Primary:
    case ButtonKind::Danger:
        if (enabled_) {
            fill = pressed_ ? theme.accent_pressed
                            : (kind_ == ButtonKind::Danger ? theme.danger : theme.accent);
            foreground = kind_ == ButtonKind::Danger
                             ? theme.accent_text
                             : (pressed_ ? theme.primary_text_pressed : theme.primary_text);
            rest_glow = 0.30f;
            hover_glow = 0.60f;
            glow_spread = Lerp(1.0f, 1.75f, glow_t_);
        } else {
            fill = theme.fill_input_disabled;
            foreground = theme.text_disabled;
        }
        break;
    case ButtonKind::Subtle:
    case ButtonKind::Transparent:
        if (enabled_) {
            Color wash = theme.fill_pressed;
            wash.a *= glow_t_;
            fill = wash;
            foreground = {theme.text.r, theme.text.g, theme.text.b,
                          Lerp(theme.text_secondary.a, theme.text.a, glow_t_)};
        } else {
            foreground = theme.text_disabled;
        }
        break;
    default:
        if (enabled_) {
            fill = theme.fill_input;
            const float wash = 0.05f * glow_t_;
            fill = {Lerp(fill.r, 1.0f, wash), Lerp(fill.g, 1.0f, wash), Lerp(fill.b, 1.0f, wash),
                    1.0f};
            if (pressed_) fill = theme.fill_input_pressed;
            border = Color{theme.accent.r, theme.accent.g, theme.accent.b,
                           Lerp(0.20f, 1.0f, glow_t_) * theme.glow_intensity};
            hover_glow = 0.30f;
        } else {
            fill = theme.fill_input_disabled;
            border = Color{theme.accent.r, theme.accent.g, theme.accent.b,
                           0.08f * theme.glow_intensity};
            foreground = theme.text_disabled;
        }
        break;
    }

    const float glow_a = Lerp(rest_glow, hover_glow, glow_t_) * theme.glow_intensity;
    if (glow_a > 0.004f) {
        painter.DrawGlow(r, radius,
                         Color{theme.glow_sm.r, theme.glow_sm.g, theme.glow_sm.b, glow_a},
                         glow_spread);
    }
    if (focused_ && enabled_) {
        painter.DrawGlow(r, radius, Color{theme.glow_sm.r, theme.glow_sm.g, theme.glow_sm.b,
                                          theme.glow_sm.a * 0.7f});
    }
    if (fill.a > 0.0f) painter.FillRoundedRect(r, radius, fill);
    if (kind_ == ButtonKind::Standard && enabled_) {
        Color inset = theme.edge_light;
        inset.a *= Lerp(0.45f, 1.0f, glow_t_);
        painter.DrawInnerLight(r, radius, inset,
                               Color{0.0f, 0.0f, 0.0f, Lerp(0.22f, 0.40f, glow_t_)});
    }
    if (border.a > 0.0f) painter.StrokeRoundedRect(r, radius, border);
    if (focused_ && enabled_ && !solid) {
        PaintFocusRing(painter, theme, r, radius);
    }

    const float chevron_slot = kChevron + m.pad_x;
    float x = r.x + m.pad_x;
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, {x, r.y, 16.0f, r.h}, 16.0f, foreground);
        x += 22.0f;
    }
    if (!text_.empty()) {
        const float text_w = std::max(0.0f, r.Right() - chevron_slot - kChevronGap - x);
        painter.DrawText(text_, {x, r.y, text_w, r.h}, role, foreground);
    }
    painter.DrawIcon(icon::kChevronDown, {r.Right() - chevron_slot, r.y, chevron_slot, r.h},
                     kChevron, foreground);
}

} // namespace lumen
