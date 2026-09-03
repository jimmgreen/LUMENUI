#include "lumen/ProgressRing.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include <cmath>

namespace lumen {

void ProgressRing::RelayoutParent() { Control::RelayoutParent(); }

ProgressRing& ProgressRing::Value(float value) {
    value = Clamp(value, 0.0f, 1.0f);
    if (!indeterminate_ && value_ == value) return *this;
    value_ = value;
    Invalidate();
    return *this;
}

ProgressRing& ProgressRing::Indeterminate(bool value) {
    if (indeterminate_ == value) return *this;
    indeterminate_ = value;
    if (value) {
        phase_ = 0.0f;
        Animate();
    }
    Invalidate();
    return *this;
}

Size ProgressRing::Measure(Size, const Theme&) {
    return {box_, box_};
}

bool ProgressRing::OnAnimate(float dt_seconds) {
    if (!indeterminate_) return Control::OnAnimate(dt_seconds);
    phase_ += dt_seconds;
    // kRange=180°：两周期 extra 恰为 360°，回绕后起点模 360 连续。
    constexpr float kPeriod = 1.8f;
    if (phase_ >= kPeriod * 2.0f) phase_ -= kPeriod * 2.0f;
    Invalidate();
    return true;
}

void ProgressRing::Draw(Painter& painter, const Theme& theme) {
    const Point center{absolute_.x + box_ * 0.5f, absolute_.y + box_ * 0.5f};
    const float radius = box_ * 0.5f - 4.0f;
    constexpr float kFullCircle = 359.99f;
    painter.DrawArc(center, radius, 0.0f, kFullCircle, theme.stroke_divider, 4.0f);
    auto lit_arc = [&](float start, float span) {
        // 辉光：宽弧低透明打底 + 主弧（阶梯发光）
        painter.DrawArc(center, radius, start, span,
                        Color{theme.glow_sm.r, theme.glow_sm.g, theme.glow_sm.b,
                              theme.glow_sm.a * 0.63f}, 9.0f);
        painter.DrawArc(center, radius, start, span, theme.accent, 4.0f);
    };
    if (indeterminate_) {
        // 前半段尾随转、头甩开加长；后半段头保持转速、尾追上缩短。
        // 禁止 span 与 start 同步反向（旧式 cos 呼吸会让后半段两端对缩）。
        constexpr float kPeriod = 1.8f;
        constexpr float kMinSpan = 80.0f;
        constexpr float kMaxSpan = 260.0f;
        constexpr float kRange = kMaxSpan - kMinSpan;
        constexpr float kPi = 3.14159265f;
        const float cycle = phase_ / kPeriod;
        const float t = cycle - std::floor(cycle);
        const float extra = kRange * std::floor(cycle);
        const float rot = -90.0f + 360.0f * cycle + extra;
        const float half = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
        const float ease = 0.5f - 0.5f * std::cos(half * kPi);
        const float span = t < 0.5f ? kMinSpan + kRange * ease : kMaxSpan - kRange * ease;
        const float start = t < 0.5f ? rot : rot + (kMaxSpan - span);
        lit_arc(start, span);
    } else if (value_ > 0.001f) {
        lit_arc(-90.0f, 359.99f * value_);
    }
}

} // namespace lumen
