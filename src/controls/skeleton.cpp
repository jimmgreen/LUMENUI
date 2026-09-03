#include "lumen/Skeleton.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <cmath>

namespace lumen {

Size Skeleton::Measure(Size, const Theme&) {
    const float w = custom_width_ > 0.5f ? custom_width_ : 160.0f;
    const float h = custom_height_ > 0.5f
                        ? custom_height_
                        : 12.0f * static_cast<float>(lines_);
    return {w, h};
}

bool Skeleton::OnAnimate(float dt_seconds) {
    if (!active_ || MotionScale() <= 0.001f) return Control::OnAnimate(dt_seconds);
    phase_ = std::fmod(phase_ + dt_seconds * 1.4f, 6.2831853f);
    Invalidate();
    return true;   // 加载占位属显式播放期，时钟在其间持续运转
}

void Skeleton::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    const float radius = round_ ? std::min(absolute_.h, 9999.0f) : theme.radius_control;
    const float line_h = absolute_.h / static_cast<float>(lines_);
    const float breathe = active_ ? 0.72f + 0.28f * std::sin(phase_) : 1.0f;
    for (int i = 0; i < lines_; ++i) {
        const bool last = i == lines_ - 1 && lines_ > 1;
        const float w = last ? absolute_.w * 0.6f : absolute_.w;
        Color fill = theme.fill_hover;
        fill.a *= breathe;
        painter.FillRoundedRect({absolute_.x, absolute_.y + static_cast<float>(i) * line_h, w,
                                 line_h - (lines_ > 1 ? 6.0f : 0.0f)},
                                radius, fill);
    }
}

} // namespace lumen
