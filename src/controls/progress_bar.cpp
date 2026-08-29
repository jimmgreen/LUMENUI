#include "fluentui/ProgressBar.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include <cmath>

namespace fui {

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
    const Rect track{absolute_.x, absolute_.y + (absolute_.h - 3.0f) * 0.5f, absolute_.w, 3.0f};
    painter.FillRoundedRect(track, 1.5f, theme.divider);
    if (indeterminate_) {
        // 双段扫描：主段 + 延迟尾段，周期 2.4s
        const float cycle = std::fmod(phase_, 2.4f);
        const float w = absolute_.w;
        const float span = w * 0.35f;
        auto segment = [&](float start, float end) {
            const float x0 = std::max(track.x, start);
            const float x1 = std::min(track.Right(), end);
            if (x1 > x0) painter.FillRoundedRect({x0, track.y, x1 - x0, track.h}, 1.5f, theme.accent);
        };
        // 段 1：0 → 1.2s 从左扫到右
        if (cycle < 1.2f) {
            const float t = cycle / 1.2f;
            const float head = -span + t * (w + span * 2.0f);
            segment(head - span, head);
        }
        // 段 2：0.4 → 2.0s 跟随
        if (cycle > 0.4f && cycle < 2.0f) {
            const float t = (cycle - 0.4f) / 1.2f;
            const float head = -span + t * (w + span * 2.0f);
            segment(head - span * 0.6f, head);
        }
    } else {
        const float fill_w = std::max(absolute_.w * value_, value_ > 0.0f ? 4.0f : 0.0f);
        if (fill_w > 0.5f) {
            painter.FillRoundedRect({track.x, track.y, std::min(fill_w, absolute_.w), track.h}, 1.5f,
                                    theme.accent);
        }
    }
}

} // namespace fui
