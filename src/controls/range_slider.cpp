#include "lumen/RangeSlider.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {
constexpr float kThumb = 14.0f;
constexpr float kTouchThumb = 20.0f;
}

float RangeSlider::ThumbDip() const noexcept {
    return (pressed_ && TouchInput()) ? kTouchThumb : kThumb;
}

RangeSlider& RangeSlider::Range(float minimum, float maximum) {
    min_ = std::min(minimum, maximum);
    max_ = std::max(minimum, maximum);
    return Values(lower_, upper_);
}

RangeSlider& RangeSlider::Values(float lower, float upper) {
    lower_ = Clamp(std::min(lower, upper), min_, max_);
    upper_ = Clamp(std::max(lower, upper), min_, max_);
    Invalidate();
    return *this;
}

RangeSlider& RangeSlider::Step(float value) {
    step_ = std::max(0.0f, value);
    return *this;
}

float RangeSlider::Snap(float value) const {
    if (step_ > 0.0f) value = min_ + std::round((value - min_) / step_) * step_;
    return Clamp(value, min_, max_);
}

float RangeSlider::Position(float value) const {
    return max_ > min_ ? Clamp((value - min_) / (max_ - min_), 0.0f, 1.0f) : 0.0f;
}

float RangeSlider::ValueAt(Point local) const {
    const bool horizontal = orientation_ == SliderOrientation::Horizontal;
    const float half = ThumbDip() * 0.5f;
    const float extent = (horizontal ? absolute_.w : absolute_.h) - half * 2.0f;
    if (extent <= 0.0f) return min_;
    const float along = horizontal ? local.x - half : absolute_.h - local.y - half;
    return Snap(min_ + (max_ - min_) * Clamp(along / extent, 0.0f, 1.0f));
}

Rect RangeSlider::ThumbRect(float value) const {
    const float t = Position(value);
    const float thumb = ThumbDip();
    const float half = thumb * 0.5f;
    if (orientation_ == SliderOrientation::Horizontal) {
        const float x = absolute_.x + half + (absolute_.w - half * 2.0f) * t;
        return {x - half, absolute_.y + absolute_.h * 0.5f - half, thumb, thumb};
    }
    const float y = absolute_.Bottom() - half - (absolute_.h - half * 2.0f) * t;
    return {absolute_.x + absolute_.w * 0.5f - half, y - half, thumb, thumb};
}

void RangeSlider::Track(Point local, bool notify) {
    const float value = ValueAt(local);
    const float old_lower = lower_;
    const float old_upper = upper_;
    if (active_ == Thumb::Lower) lower_ = std::min(value, upper_);
    else upper_ = std::max(value, lower_);
    if (old_lower == lower_ && old_upper == upper_) return;
    Invalidate();
    if (notify) changed_.Emit(lower_, upper_);
}

Size RangeSlider::Measure(Size, const Theme&) {
    return orientation_ == SliderOrientation::Horizontal ? Size{220.0f, 24.0f}
                                                         : Size{24.0f, 220.0f};
}

void RangeSlider::Draw(Painter& painter, const Theme& theme) {
    const bool horizontal = orientation_ == SliderOrientation::Horizontal;
    const float half = ThumbDip() * 0.5f;
    const Rect track = horizontal
                           ? Rect{absolute_.x + half, absolute_.y + absolute_.h * 0.5f - 2.0f,
                                  absolute_.w - half * 2.0f, 4.0f}
                           : Rect{absolute_.x + absolute_.w * 0.5f - 2.0f, absolute_.y + half,
                                  4.0f, absolute_.h - half * 2.0f};
    painter.FillRoundedRect(track, 2.0f,
                            Color{theme.accent.r, theme.accent.g, theme.accent.b,
                                  0.20f * theme.glow_intensity});
    const float lo = Position(lower_);
    const float hi = Position(upper_);
    const Rect selected = horizontal
                              ? Rect{track.x + track.w * lo, track.y, track.w * (hi - lo), track.h}
                              : Rect{track.x, track.Bottom() - track.h * hi, track.w,
                                     track.h * (hi - lo)};
    if (!selected.IsEmpty()) {
        painter.FillRoundedRect(selected, 2.0f, enabled_ ? theme.accent : theme.text_disabled);
    }
    const Rect lower = ThumbRect(lower_);
    const Rect upper = ThumbRect(upper_);
    const auto paint_thumb = [&](const Rect& thumb, bool active) {
        const float radius = thumb.w * 0.5f;
        if (enabled_) painter.DrawGlow(thumb, radius, theme.glow_sm);
        painter.FillRoundedRect(thumb, radius, enabled_ ? theme.accent : theme.text_disabled);
        if (focused_ && enabled_ && active) {
            PaintFocusRing(painter, theme, thumb, radius);
        }
    };
    paint_thumb(lower, active_ == Thumb::Lower);
    paint_thumb(upper, active_ == Thumb::Upper);
}

bool RangeSlider::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_TAB) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!shift && active_ == Thumb::Lower) { active_ = Thumb::Upper; Invalidate(); return true; }
        if (shift && active_ == Thumb::Upper) { active_ = Thumb::Lower; Invalidate(); return true; }
        return false;
    }
    const bool horizontal = orientation_ == SliderOrientation::Horizontal;
    const float delta = step_ > 0.0f ? step_ : (max_ - min_) * 0.05f;
    float value = active_ == Thumb::Lower ? lower_ : upper_;
    if (vk == VK_HOME) value = active_ == Thumb::Lower ? min_ : lower_;
    else if (vk == VK_END) value = active_ == Thumb::Upper ? max_ : upper_;
    else if ((horizontal && vk == VK_LEFT) || (!horizontal && vk == VK_DOWN)) value -= delta;
    else if ((horizontal && vk == VK_RIGHT) || (!horizontal && vk == VK_UP)) value += delta;
    else return false;
    const float old_lower = lower_;
    const float old_upper = upper_;
    value = Snap(value);
    if (active_ == Thumb::Lower) lower_ = std::min(value, upper_);
    else upper_ = std::max(value, lower_);
    if (old_lower != lower_ || old_upper != upper_) {
        Invalidate();
        changed_.Emit(lower_, upper_);
    }
    return true;
}

void RangeSlider::OnMouseDown(Point local, uint32_t buttons) {
    if (!enabled_ || !(buttons & MK_LBUTTON)) return;
    Focus();
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    const Rect lower = ThumbRect(lower_);
    const Rect upper = ThumbRect(upper_);
    const auto distance = [&](const Rect& r) {
        const float dx = world.x - (r.x + r.w * 0.5f);
        const float dy = world.y - (r.y + r.h * 0.5f);
        return dx * dx + dy * dy;
    };
    active_ = distance(lower) <= distance(upper) ? Thumb::Lower : Thumb::Upper;
    pressed_ = true;
    Track(local, true);
}

void RangeSlider::OnMouseMove(Point local, uint32_t buttons) {
    if (!enabled_ || !pressed_ || !(buttons & MK_LBUTTON)) return;
    Track(local, true);
}

void RangeSlider::OnMouseUp(Point, uint32_t) {
    pressed_ = false;
    Invalidate();
}

void RangeSlider::OnFocusChanged(bool focused) {
    focused_ = focused;
    if (focused) active_ = Thumb::Lower;
    Invalidate();
}

} // namespace lumen
