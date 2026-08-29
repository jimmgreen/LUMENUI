#include "fluentui/Switch.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"

namespace fui {
namespace {
constexpr float kTrackW = 40.0f;
constexpr float kTrackH = 20.0f;
constexpr float kKnob = 12.0f;
constexpr float kGap = 10.0f;

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}
} // namespace

void Switch::RelayoutParent() { Control::RelayoutParent(); }

void Switch::SetChecked(bool value) {
    if (checked_ == value) return;
    checked_ = value;
    if (window_) {
        Animate();
    } else {
        knob_t_ = checked_ ? 1.0f : 0.0f;   // 无窗口时钟（离屏渲染）直接到位
    }
    Invalidate();
}

Size Switch::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kTrackW : kTrackW + kGap +
                        UiText().MeasureText(text_, TextRole::Body).w;
    return {width, 28.0f};
}

bool Switch::OnAnimate(float dt_seconds) {
    bool moving = Control::OnAnimate(dt_seconds);
    moving |= EaseTo(knob_t_, checked_ ? 1.0f : 0.0f, dt_seconds, 18.0f);
    return moving;
}

void Switch::Draw(Painter& painter, const Theme& theme) {
    const Rect track{absolute_.x, absolute_.y + (absolute_.h - kTrackH) * 0.5f, kTrackW, kTrackH};
    Color track_fill = theme.control_fill;
    Color border = theme.control_stroke_strong;
    Color knob_color = theme.text_secondary;
    if (knob_t_ > 0.0f) {
        track_fill = Mix(theme.control_fill, theme.accent, knob_t_);
        border = Mix(theme.control_stroke_strong, theme.accent, knob_t_);
    }
    if (!enabled_) {
        track_fill.a *= 0.5f;
        border.a *= 0.5f;
        knob_color = theme.text_disabled;
    }
    painter.FillRoundedRect(track, kTrackH * 0.5f, track_fill);
    painter.StrokeRoundedRect(track, kTrackH * 0.5f, border);
    if (focused_ && enabled_) painter.DrawFocusRing(track, kTrackH * 0.5f, theme.focus_ring);

    // 滑块沿轨道滑动（左右各留 4 DIP 边距）
    const float travel = kTrackW - kKnob - 8.0f;
    const float knob_x = track.x + 4.0f + travel * knob_t_;
    const Rect knob{knob_x, track.y + (kTrackH - kKnob) * 0.5f, kKnob, kKnob};
    Color final_knob = knob_t_ > 0.5f ? theme.accent_text : knob_color;
    if (!enabled_) final_knob = theme.text_disabled;
    painter.FillRoundedRect(knob, kKnob * 0.5f, final_knob);

    if (!text_.empty()) {
        painter.DrawText(text_,
                         {absolute_.x + kTrackW + kGap, absolute_.y,
                          UiText().MeasureText(text_, TextRole::Body).w + 2.0f, absolute_.h},
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
    Animate();
    if (toggled_) toggled_();
    Invalidate();
}

bool Switch::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        checked_ = !checked_;
        Animate();
        if (toggled_) toggled_();
        Invalidate();
        return true;
    }
    return false;
}

} // namespace fui
