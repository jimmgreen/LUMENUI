#include "fluentui/Slider.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"

namespace fui {

void Slider::RelayoutParent() { Control::RelayoutParent(); }

void Slider::SetRange(float min_value, float max_value) {
    min_ = std::min(min_value, max_value);
    max_ = max_value;
    SetValue(value_);
}

void Slider::SetValue(float value) {
    value = Clamp(value, min_, max_);
    if (value_ == value) return;
    value_ = value;
    Invalidate();
}

Size Slider::Measure(Size, const Theme&) {
    return {180.0f, 24.0f};
}

void Slider::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

void Slider::TrackThumb(Point local) {
    const float half = 9.0f;
    const float track_w = absolute_.w - half * 2.0f;
    if (track_w <= 0.0f) return;
    const float t = Clamp((local.x - half) / track_w, 0.0f, 1.0f);
    const float new_value = min_ + (max_ - min_) * t;
    if (value_ != new_value) {
        value_ = new_value;
        Invalidate();
        if (changed_) changed_();
    }
}

void Slider::Draw(Painter& painter, const Theme& theme) {
    const float track_y = absolute_.y + absolute_.h * 0.5f;
    const float half = 9.0f;
    const float t = max_ > min_ ? (value_ - min_) / (max_ - min_) : 0.0f;

    const Rect track{absolute_.x + half, track_y - 2.0f, absolute_.w - half * 2.0f, 4.0f};
    painter.FillRoundedRect(track, 2.0f, theme.divider);
    if (t > 0.001f) {
        const float fill_w = std::max(track.w * t, 4.0f);
        painter.FillRoundedRect({track.x, track.y, std::min(fill_w, track.w), track.h}, 2.0f,
                                enabled_ ? theme.accent : theme.text_disabled);
    }
    const float knob = 18.0f + press_t_ * 4.0f;
    const Rect thumb{absolute_.x + half + (absolute_.w - half * 2.0f) * t - knob * 0.5f,
                     track_y - knob * 0.5f, knob, knob};
    painter.FillRoundedRect(thumb, knob * 0.5f,
                            enabled_ ? theme.accent : theme.text_disabled);
    if (focused_ && enabled_) {
        painter.DrawFocusRing(thumb, knob * 0.5f, theme.focus_ring, 2.0f);
    }
}

bool Slider::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    const float step = (max_ - min_) * 0.05f;
    float new_value = value_;
    switch (vk) {
    case VK_LEFT: new_value -= step; break;
    case VK_RIGHT: new_value += step; break;
    case VK_HOME: new_value = min_; break;
    case VK_END: new_value = max_; break;
    default: return false;
    }
    new_value = Clamp(new_value, min_, max_);
    if (new_value != value_) {
        value_ = new_value;
        Invalidate();
        if (changed_) changed_();
    }
    return true;
}

void Slider::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    pressed_ = true;
    Animate();
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
    Animate();
}

} // namespace fui
