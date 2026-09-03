#include "lumen/Slider.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include <cmath>

namespace lumen {

void Slider::RelayoutParent() { Control::RelayoutParent(); }

Slider& Slider::Range(float min_value, float max_value) {
    min_ = std::min(min_value, max_value);
    max_ = std::max(min_value, max_value);
    Value(value_);
    return *this;
}

Slider& Slider::Value(float value) {
    value = Clamp(value, min_, max_);
    if (value_ == value) return *this;
    value_ = value;
    Invalidate();
    return *this;
}

Size Slider::Measure(Size, const Theme&) {
    return orientation_ == SliderOrientation::Horizontal ? Size{180.0f, 24.0f}
                                                         : Size{24.0f, 180.0f};
}

void Slider::OnFocusChanged(bool focused) {
    focused_ = focused;
    Invalidate();
}

void Slider::TrackThumb(Point local) {
    const float half = KnobDip() * 0.5f;
    const float extent = (orientation_ == SliderOrientation::Horizontal ? absolute_.w
                                                                        : absolute_.h) - half * 2.0f;
    if (extent <= 0.0f) return;
    const float along = orientation_ == SliderOrientation::Horizontal ? local.x - half
                                                                      : absolute_.h - local.y - half;
    const float t = Clamp(along / extent, 0.0f, 1.0f);
    float new_value = min_ + (max_ - min_) * t;
    if (step_ > 0.0f) new_value = min_ + std::round((new_value - min_) / step_) * step_;
    new_value = Clamp(new_value, min_, max_);
    if (value_ != new_value) {
        value_ = new_value;
        Invalidate();
        changed_.Emit(value_);
    }
}

float Slider::KnobDip() const noexcept {
    return (pressed_ && TouchInput()) ? 20.0f : 14.0f;
}

void Slider::Draw(Painter& painter, const Theme& theme) {
    const float knob = KnobDip();
    const float half = knob * 0.5f;
    const float t = max_ > min_ ? (value_ - min_) / (max_ - min_) : 0.0f;
    const bool horizontal = orientation_ == SliderOrientation::Horizontal;
    const Rect track = horizontal
                           ? Rect{absolute_.x + half, absolute_.y + absolute_.h * 0.5f - 2.0f,
                                  absolute_.w - half * 2.0f, 4.0f}
                           : Rect{absolute_.x + absolute_.w * 0.5f - 2.0f, absolute_.y + half,
                                  4.0f, absolute_.h - half * 2.0f};
    painter.FillRoundedRect(track, 2.0f,
                             Color{theme.accent.r, theme.accent.g, theme.accent.b,
                                   0.20f * theme.glow_intensity});
    if (t > 0.001f) {
        const Rect fill = horizontal
                              ? Rect{track.x, track.y, std::min(std::max(track.w * t, 4.0f), track.w), track.h}
                              : Rect{track.x, track.Bottom() - std::min(std::max(track.h * t, 4.0f), track.h),
                                     track.w, std::min(std::max(track.h * t, 4.0f), track.h)};
        painter.FillRoundedRect(fill, 2.0f, enabled_ ? theme.accent : theme.text_disabled);
    }
    const Rect thumb = horizontal
                           ? Rect{track.x + track.w * t - knob * 0.5f,
                                  absolute_.y + absolute_.h * 0.5f - knob * 0.5f, knob, knob}
                           : Rect{absolute_.x + absolute_.w * 0.5f - knob * 0.5f,
                                  track.Bottom() - track.h * t - knob * 0.5f, knob, knob};
    if (enabled_) {
        painter.DrawGlow(thumb, knob * 0.5f, theme.glow_sm);
        painter.FillRoundedRect(thumb, knob * 0.5f, theme.accent);
    } else {
        painter.FillRoundedRect(thumb, knob * 0.5f, theme.text_disabled);
    }
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, thumb, knob * 0.5f);
    }
}

bool Slider::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    const float step = step_ > 0.0f ? step_ : (max_ - min_) * 0.05f;
    float new_value = value_;
    switch (vk) {
    case VK_LEFT: if (orientation_ == SliderOrientation::Horizontal) new_value -= step; else return false; break;
    case VK_RIGHT: if (orientation_ == SliderOrientation::Horizontal) new_value += step; else return false; break;
    case VK_DOWN: if (orientation_ == SliderOrientation::Vertical) new_value -= step; else return false; break;
    case VK_UP: if (orientation_ == SliderOrientation::Vertical) new_value += step; else return false; break;
    case VK_HOME: new_value = min_; break;
    case VK_END: new_value = max_; break;
    default: return false;
    }
    new_value = Clamp(new_value, min_, max_);
    if (new_value != value_) {
        value_ = new_value;
        Invalidate();
        changed_.Emit(value_);
    }
    return true;
}

void Slider::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    pressed_ = true;
    Invalidate();
    TrackThumb(local);
}

void Slider::OnMouseMove(Point local, uint32_t buttons) {
    if (!enabled_ || !pressed_ || !(buttons & MK_LBUTTON)) return;
    TrackThumb(local);
}

void Slider::OnMouseUp(Point local, uint32_t buttons) {
    (void)local;
    (void)buttons;
    pressed_ = false;
    Invalidate();
}

Slider& Slider::BindValue(Property<float>& p, float scale) {
    if (scale == 0.0f) scale = 1.0f;
    auto apply = [this, &p, scale] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Value(p.Get() * scale);
        bind_loop_ = false;
    };
    apply();
    value_prop_ = ScopedConnection(p.OnChanged([apply](const float&) { apply(); }));
    value_ctrl_ = ScopedConnection(changed_.Connect([this, &p, scale](float v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = v / scale;
        bind_loop_ = false;
    }));
    return *this;
}

Slider& Slider::BindValue(Property<double>& p, double scale) {
    if (scale == 0.0) scale = 1.0;
    auto apply = [this, &p, scale] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Value(static_cast<float>(p.Get() * scale));
        bind_loop_ = false;
    };
    apply();
    value_prop_ = ScopedConnection(p.OnChanged([apply](const double&) { apply(); }));
    value_ctrl_ = ScopedConnection(changed_.Connect([this, &p, scale](float v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = static_cast<double>(v) / scale;
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
