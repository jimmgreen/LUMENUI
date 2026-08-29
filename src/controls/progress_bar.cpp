#include "fluentui/ProgressBar.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include <cmath>

namespace fui {
namespace {

Color Rgba2(uint32_t rgb, float alpha) { return Color::Hex(rgb, alpha); }

} // namespace

void ProgressBar::RelayoutParent() { Control::RelayoutParent(); }

void ProgressBar::SetValue(float value) {
    value = Clamp(value, 0.0f, 1.0f);
    if (!indeterminate_ && value_ == value) return;
    value_ = value;
    Invalidate();
}

void ProgressBar::SetIndeterminate(bool value) {
    if (indeterminate_ == value) return;
    indeterminate_ = value;
    if (value) {
        phase_ = 0.0f;
        Animate();
    }
    Invalidate();
}

Size ProgressBar::Measure(Size, const Theme&) {
    return {160.0f, 4.0f};
}

bool ProgressBar::OnAnimate(float dt_seconds) {
    if (!indeterminate_) return Control::OnAnimate(dt_seconds);
    phase_ += dt_seconds;
    Invalidate();
    return true;
}

void ProgressBar::Draw(Painter& painter, const Theme& theme) {
    // 底轨是 1px 直线；填充条高 4px 圆角
    const float bar_h = 3.0f;
    const float cy = absolute_.y + absolute_.h * 0.5f;
    const Rect track{absolute_.x, cy - bar_h * 0.5f, absolute_.w, bar_h};
    painter.FillRect({track.x, cy - 0.5f, track.w, 1.0f},
                     theme.dark ? Rgba2(0xFFFFFF, 155.0f / 255.0f) : Rgba2(0x000000, 155.0f / 255.0f));
    if (indeterminate_) {
        // 双条循环，总周期 1952ms：短条 833ms 线性、长条 1167ms OutQuad 且延迟 785ms
        const float cycle_ms = std::fmod(phase_ * 1000.0f, 1952.0f);
        const float w = track.w;
        auto bar = [&](float pos, float bar_w) {
            const float left = (pos - bar_w) * w;
            const float right = pos * w;
            if (right <= track.x || left >= track.Right()) return;
            painter.FillRoundedRect({std::max(left, track.x), track.y,
                                     std::min(right, track.Right()) - std::max(left, track.x),
                                     track.h},
                                    track.h * 0.5f, theme.accent);
        };
        const auto linear = [](float t) { return Clamp(t, 0.0f, 1.0f); };
        const auto out_quad = [](float t) {
            t = Clamp(t, 0.0f, 1.0f);
            return 1.0f - (1.0f - t) * (1.0f - t);
        };
        bar(1.45f * linear(cycle_ms / 833.0f), 0.4f);
        bar(1.75f * out_quad((cycle_ms - 785.0f) / 1167.0f), 0.6f);
    } else {
        const float fill_w = std::max(track.w * value_, value_ > 0.0f ? 4.0f : 0.0f);
        if (fill_w > 0.5f) {
            painter.FillRoundedRect({track.x, track.y, std::min(fill_w, track.w), track.h},
                                    track.h * 0.5f, theme.accent);
        }
    }
}

} // namespace fui
