#include "lumen/ToggleButton.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace lumen {

void ToggleButton::RelayoutParent() { Control::RelayoutParent(); }

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
TextRole RoleOf(ButtonSize size) {
    return size == ButtonSize::Small ? TextRole::Caption : TextRole::Body;
}
}  // namespace

ToggleButton& ToggleButton::Checked(bool value) {
    if (checked_ == value) return *this;
    checked_ = value;
    Invalidate();
    return *this;
}

void ToggleButton::Toggle() {
    checked_ = !checked_;
    toggled_.Emit(checked_);
    Invalidate();
}

Size ToggleButton::Measure(Size, const Theme&) {
    const Metrics m = SizeMetrics(size_);
    const TextRole role = RoleOf(size_);
    if (glyph_.empty() && text_.empty()) return {m.height, m.height};
    if (!glyph_.empty() && text_.empty()) return {m.height, m.height};
    float width = m.pad_x * 2.0f;
    if (!glyph_.empty()) width += 16.0f + 6.0f;
    if (!text_.empty()) width += MeasureText(text_, role).w;
    if (pill_) width = std::max(width, m.height);
    return {std::max(width, m.height), m.height};
}

void ToggleButton::OnMouseEnter() {
    Control::OnMouseEnter();
    Animate();
}
void ToggleButton::OnMouseLeave() {
    Control::OnMouseLeave();
    Animate();
}
void ToggleButton::OnMouseDown(Point, uint32_t) {
    if (!enabled_) return;
    pressed_ = true;
    Animate();
}
void ToggleButton::OnMouseUp(Point local, uint32_t) {
    const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
                        local.y <= absolute_.h;
    if (pressed_) {
        pressed_ = false;
        Animate();
    }
    if (enabled_ && inside) Toggle();
}
bool ToggleButton::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE || vk == VK_RETURN) {
        Toggle();
        return true;
    }
    return false;
}
void ToggleButton::OnFocusChanged(bool focused) {
    focused_ = focused;
    Animate();
    Invalidate();
}
bool ToggleButton::OnAnimate(float dt) {
    const bool lit = (hovered_ || focused_) && enabled_;
    bool active = Control::OnAnimate(dt);
    active |= EaseTo(glow_t_, lit ? 1.0f : 0.0f, dt, 14.0f);
    active |= EaseTo(scale_t_, pressed_ && enabled_ ? 1.0f : 0.0f, dt, 20.0f);
    return active;
}

void ToggleButton::Draw(Painter& painter, const Theme& theme) {
    const TextRole role = RoleOf(size_);
    const float shrink = 0.02f * scale_t_;
    const Rect r = scale_t_ > 0.001f
                       ? absolute_.Inset(absolute_.w * shrink * 0.5f, absolute_.h * shrink * 0.5f)
                       : absolute_;
    const float radius = pill_ ? r.h * 0.5f : theme.radius_control;

    Color fill = theme.fill_input;
    Color border{theme.accent.r, theme.accent.g, theme.accent.b,
                 0.20f * theme.glow_intensity};
    Color foreground = theme.text;
    float glow_a = 0.0f;
    if (!enabled_) {
        fill = theme.fill_input_disabled;
        border.a = 0.08f * theme.glow_intensity;
        foreground = theme.text_disabled;
    } else if (checked_) {
        fill = theme.fill_selected;
        border.a = Lerp(0.35f, 0.85f, glow_t_) * theme.glow_intensity;
        glow_a = 0.35f * glow_t_ * theme.glow_intensity;
        if (pressed_) fill = theme.fill_input_pressed;
    } else {
        const float wash = 0.05f * glow_t_;
        fill = {Lerp(fill.r, 1.0f, wash), Lerp(fill.g, 1.0f, wash), Lerp(fill.b, 1.0f, wash),
                1.0f};
        if (pressed_) fill = theme.fill_input_pressed;
        border.a = Lerp(0.20f, 1.0f, glow_t_) * theme.glow_intensity;
        glow_a = 0.30f * glow_t_ * theme.glow_intensity;
    }

    if (glow_a > 0.004f) {
        if (pill_) {
            // Dilated capsule, not DrawGlow's AABB quads (those flash box ears on a pill).
            const float ring = 5.0f;
            Color halo = theme.glow_sm;
            halo.a = glow_a * 0.55f;
            painter.FillRoundedRect(r.Inset(-ring, -ring), radius + ring, halo);
        } else {
            painter.DrawGlow(r, radius, Color{theme.glow_sm.r, theme.glow_sm.g, theme.glow_sm.b, glow_a},
                             1.0f);
        }
    }
    painter.FillRoundedRect(r, radius, fill);
    if (enabled_ && !pill_) {
        Color inset = theme.edge_light;
        inset.a *= Lerp(0.45f, 1.0f, glow_t_);
        painter.DrawInnerLight(r, radius, inset,
                               Color{0.0f, 0.0f, 0.0f, Lerp(0.22f, 0.40f, glow_t_)});
    }
    if (border.a > 0.0f) painter.StrokeRoundedRect(r, radius, border);
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, r, radius);
    }

    const float glyph_w = glyph_.empty() ? 0.0f : 16.0f;
    const float text_w = text_.empty() ? 0.0f : MeasureText(text_, role).w;
    const float gap = (glyph_w > 0.0f && text_w > 0.0f) ? 6.0f : 0.0f;
    const float content_w = glyph_w + gap + text_w;
    float x = r.x + (r.w - content_w) * 0.5f;
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, {x, r.y, glyph_w, r.h}, glyph_w, foreground);
        x += glyph_w + gap;
    }
    if (!text_.empty()) {
        painter.DrawText(text_, {x, r.y, text_w + 2.0f, r.h}, role, foreground);
    }
}

ToggleButton& ToggleButton::BindChecked(Property<bool>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Checked(p.Get());
        bind_loop_ = false;
    };
    apply();
    checked_bind_ = ScopedConnection(p.OnChanged([apply](const bool&) { apply(); }));
    checked_ctrl_ = ScopedConnection(toggled_.Connect([this, &p](bool v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = v;
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
