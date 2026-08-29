#include "fluentui/Switch.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include <cmath>

namespace fui {
namespace {
constexpr float kTrackW = 42.0f;
constexpr float kTrackH = 22.0f;
constexpr float kKnob = 12.0f;      // 滑块直径
constexpr float kTravel = 20.0f;    // 滑块行程（120ms 线性）
constexpr float kGap = 12.0f;

Color Rgba(uint32_t rgb, float alpha) { return Color::Hex(rgb, alpha); }

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
    // 120ms 线性滑动
    const float target = checked_ ? 1.0f : 0.0f;
    const float step = dt_seconds / 0.12f;
    if (knob_t_ == target) return false;
    if (step >= std::fabs(target - knob_t_)) {
        knob_t_ = target;
        return false;
    }
    knob_t_ += target > knob_t_ ? step : -step;
    Invalidate();
    return true;
}

void Switch::Draw(Painter& painter, const Theme& theme) {
    const Rect track{absolute_.x, absolute_.y + (absolute_.h - kTrackH) * 0.5f, kTrackW, kTrackH};

    Color track_fill, border, knob_color;
    if (checked_) {
        track_fill = theme.accent;
        border = track_fill;
        knob_color = theme.dark ? Color::Hex(0x000000) : Color::Hex(0xFFFFFF);
        if (hovered_) track_fill = border = theme.accent_hover;
        if (pressed_) track_fill = border = theme.accent_pressed;
        if (!enabled_) {
            track_fill = border = theme.dark ? Rgba(0xFFFFFF, 41.0f / 255.0f)
                                             : Rgba(0x000000, 56.0f / 255.0f);
            knob_color = theme.dark ? Rgba(0xFFFFFF, 77.0f / 255.0f) : Color::Hex(0xFFFFFF);
        }
    } else {
        track_fill = Color{0, 0, 0, 0};
        border = theme.dark ? Rgba(0xFFFFFF, 153.0f / 255.0f) : Rgba(0x000000, 133.0f / 255.0f);
        knob_color = theme.dark ? Rgba(0xFFFFFF, 201.0f / 255.0f) : Rgba(0x000000, 156.0f / 255.0f);
        if (enabled_) {
            if (hovered_) {
                track_fill = theme.dark ? Rgba(0xFFFFFF, 10.0f / 255.0f)
                                        : Rgba(0x000000, 15.0f / 255.0f);
            }
            if (pressed_) {
                track_fill = theme.dark ? Rgba(0xFFFFFF, 18.0f / 255.0f)
                                        : Rgba(0x000000, 23.0f / 255.0f);
            }
        } else {
            border = theme.dark ? Rgba(0xFFFFFF, 41.0f / 255.0f) : Rgba(0x000000, 56.0f / 255.0f);
            knob_color = theme.dark ? Rgba(0xFFFFFF, 96.0f / 255.0f) : Rgba(0x000000, 91.0f / 255.0f);
        }
    }

    // 轨道整体内缩 1px 后绘制胶囊（fill+stroke 同一形状）
    const Rect painted = track.Inset(1.0f, 1.0f);
    painter.FillRoundedRect(painted, painted.h * 0.5f, track_fill);
    painter.StrokeRoundedRect(painted, painted.h * 0.5f,
                              track_fill.a > 0.0f && checked_ ? track_fill : border);
    if (focused_ && enabled_) {
        painter.DrawFocusRing(track, kTrackH * 0.5f, theme.accent, theme.focus_ring_width);
    }

    // 滑块：knob_x = left + 11 + 20 × 进度
    const float knob_x = track.x + 11.0f + kTravel * knob_t_;
    const Rect knob{knob_x, track.y + (kTrackH - kKnob) * 0.5f, kKnob, kKnob};
    painter.FillRoundedRect(knob, kKnob * 0.5f, knob_color);

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
    if (window_) Animate();
    else knob_t_ = checked_ ? 1.0f : 0.0f;
    if (toggled_) toggled_();
    Invalidate();
}

bool Switch::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        checked_ = !checked_;
        if (window_) Animate();
        else knob_t_ = checked_ ? 1.0f : 0.0f;
        if (toggled_) toggled_();
        Invalidate();
        return true;
    }
    return false;
}

} // namespace fui
