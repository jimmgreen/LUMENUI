#include "lumen/ProgressBar.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include <cmath>

namespace lumen {

void ProgressBar::RelayoutParent() { Control::RelayoutParent(); }

ProgressBar& ProgressBar::Value(float value) {
    value = Clamp(value, 0.0f, 1.0f);
    if (!indeterminate_ && value_ == value) return *this;
    value_ = value;
    Invalidate();
    return *this;
}

ProgressBar& ProgressBar::Indeterminate(bool value) {
    if (indeterminate_ == value) return *this;
    indeterminate_ = value;
    if (value) {
        phase_ = 0.0f;
        Animate();
    }
    Invalidate();
    return *this;
}

Size ProgressBar::Measure(Size, const Theme&) {
    return {240.0f, 8.0f};
}

bool ProgressBar::OnAnimate(float dt_seconds) {
    if (!indeterminate_) return Control::OnAnimate(dt_seconds);
    phase_ += dt_seconds;
    Invalidate();
    return true;
}

void ProgressBar::Draw(Painter& painter, const Theme& theme) {
    const float bar_h = 6.0f;
    const float cy = absolute_.y + absolute_.h * 0.5f;
    const Rect track{absolute_.x, cy - bar_h * 0.5f, absolute_.w, bar_h};
    painter.FillRoundedRect(track, bar_h * 0.5f,
                             Color{theme.accent.r, theme.accent.g, theme.accent.b,
                                   0.10f * theme.glow_intensity});
    auto lit_bar = [&](const Rect& bar) {
        if (bar.w < 0.5f) return;
        painter.DrawGlow(bar, bar.h * 0.5f, theme.glow_sm);
        painter.FillRoundedRect(bar, bar.h * 0.5f, theme.accent);
    };
    if (indeterminate_) {
        // 双条循环，总周期 1952ms：短条 833ms 线性、长条 1167ms OutQuad 且延迟 785ms
        const float cycle_ms = std::fmod(phase_ * 1000.0f, 1952.0f);
        const float w = track.w;
        auto bar = [&](float pos, float bar_w) {
            const float left = (pos - bar_w) * w;
            const float right = pos * w;
            if (right <= track.x || left >= track.Right()) return;
            lit_bar({std::max(left, track.x), track.y,
                     std::min(right, track.Right()) - std::max(left, track.x), track.h});
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
            lit_bar({track.x, track.y, std::min(fill_w, track.w), track.h});
        }
    }
}

} // namespace lumen
