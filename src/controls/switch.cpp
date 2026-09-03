#include "lumen/Switch.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"

namespace lumen {
namespace {
// GlowToggle：w-11 h-6 = 44×24，knob 16，左右 4px 内边距。
constexpr float kTrackW = 44.0f;
constexpr float kTrackH = 24.0f;
constexpr float kPad = 4.0f;
constexpr float kKnob = 16.0f;
constexpr float kGap = 12.0f;

} // namespace

void Switch::RelayoutParent() { Control::RelayoutParent(); }

Switch& Switch::Checked(bool value) {
    if (checked_ == value) return *this;
    checked_ = value;
    knob_t_ = checked_ ? 1.0f : 0.0f;   // 编程赋值到位，避免隐藏页从未进动画时钟
    Invalidate();
    return *this;
}

Size Switch::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kTrackW : kTrackW + kGap +
                        MeasureText(text_, TextRole::Body).w;
    return {width, 28.0f};
}

bool Switch::OnAnimate(float dt_seconds) {
    return EaseTo(knob_t_, checked_ ? 1.0f : 0.0f, dt_seconds, 14.0f) ||
           Control::OnAnimate(dt_seconds);
}

void Switch::Draw(Painter& painter, const Theme& theme) {
    const Rect track{absolute_.x, absolute_.y + (absolute_.h - kTrackH) * 0.5f, kTrackW,
                     kTrackH};
    const float radius = kTrackH * 0.5f;
    const float t = Clamp(knob_t_, 0.0f, 1.0f);

    const Color off_fill = enabled_ ? (hovered_ || pressed_ ? theme.fill_pressed : theme.fill_hover)
                                    : theme.fill_input_disabled;
    const Color on_fill = enabled_ ? (pressed_ ? theme.accent_pressed
                                               : (hovered_ ? theme.accent_hover : theme.accent))
                                   : theme.fill_input_disabled;
    const Color track_fill{Lerp(off_fill.r, on_fill.r, t), Lerp(off_fill.g, on_fill.g, t),
                           Lerp(off_fill.b, on_fill.b, t), Lerp(off_fill.a, on_fill.a, t)};
    const Color off_knob = enabled_ ? theme.text : theme.text_disabled;
    const Color on_knob = enabled_ ? theme.primary_text : theme.text_disabled;
    const Color knob_color{Lerp(off_knob.r, on_knob.r, t), Lerp(off_knob.g, on_knob.g, t),
                           Lerp(off_knob.b, on_knob.b, t), Lerp(off_knob.a, on_knob.a, t)};
    Color border = theme.control_stroke;
    border.a *= (1.0f - t) * (enabled_ ? (hovered_ ? 1.0f : 0.85f) : 0.4f);

    if (t > 0.04f && enabled_) {
        Color glow = theme.glow_md;
        glow.a *= t;
        painter.DrawGlow(track, radius, glow);
    }
    painter.FillRoundedRect(track, radius, track_fill);
    if (border.a > 0.004f) painter.StrokeRoundedRect(track, radius, border);
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, track, radius);
    }

    const float x0 = track.x + kPad;
    const float x1 = track.x + kTrackW - kPad - kKnob;
    const Rect knob{x0 + (x1 - x0) * t, track.y + kPad, kKnob, kKnob};
    painter.FillRoundedRect(knob, kKnob * 0.5f, knob_color);

    if (!text_.empty()) {
        painter.DrawText(text_,
                         {absolute_.x + kTrackW + kGap, absolute_.y,
                          MeasureText(text_, TextRole::Body).w, absolute_.h},
                         TextRole::Body, enabled_ ? theme.text : theme.text_disabled);
    }
}

void Switch::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    if (local.x < -4.0f || local.y < -4.0f || local.x > absolute_.w + 4.0f ||
        local.y > absolute_.h + 4.0f)
        return;
    checked_ = !checked_;
    if (window_) Animate();
    else knob_t_ = checked_ ? 1.0f : 0.0f;
    toggled_.Emit(checked_);
    Invalidate();
}

bool Switch::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        checked_ = !checked_;
        if (window_) Animate();
        else knob_t_ = checked_ ? 1.0f : 0.0f;
        toggled_.Emit(checked_);
        Invalidate();
        return true;
    }
    return false;
}

Switch& Switch::BindChecked(Property<bool>& p) {
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
