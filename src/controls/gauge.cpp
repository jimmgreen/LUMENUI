#include "lumen/Gauge.h"
#include "lumen/Painter.h"
#include "../core/chart_geom.h"
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {
constexpr float kStart = 150.0f;   // 左下开口
constexpr float kSweep = 240.0f;
} // namespace

void Gauge::RelayoutParent() { Control::RelayoutParent(); }

Gauge& Gauge::Range(float min_value, float max_value) {
    min_ = std::min(min_value, max_value);
    max_ = std::max(min_value, max_value);
    if (max_ - min_ < 1.0e-6f) max_ = min_ + 1.0f;
    value_ = Clamp(value_, min_, max_);
    Invalidate();
    return *this;
}

Gauge& Gauge::Value(float value) {
    value_ = Clamp(value, min_, max_);
    Invalidate();
    return *this;
}

Size Gauge::Measure(Size, const Theme&) { return {120.0f, 110.0f}; }

void Gauge::Draw(Painter& painter, const Theme& theme) {
    const Point c{absolute_.x + absolute_.w * 0.5f, absolute_.y + absolute_.h * 0.52f};
    const float radius = std::min(absolute_.w, absolute_.h) * 0.38f;
    painter.DrawArc(c, radius, kStart, kSweep, theme.stroke_divider, 6.0f);
    const float span = std::max(1.0e-6f, max_ - min_);
    const float t = Clamp((value_ - min_) / span, 0.0f, 1.0f);
    const float over = value_ >= threshold_ ? 1.0f : 0.0f;
    const Color arc = over > 0.5f ? theme.text : theme.text_secondary;
    if (over > 0.5f) {
        painter.DrawArc(c, radius, kStart, kSweep * t,
                        Color{theme.glow_sm.r, theme.glow_sm.g, theme.glow_sm.b,
                              theme.glow_sm.a * 0.7f},
                        10.0f);
    }
    painter.DrawArc(c, radius, kStart, kSweep * t, arc, 7.0f);
    const float end_a = (kStart + kSweep * t) * 3.14159265f / 180.0f;
    const Point cap{c.x + std::cos(end_a) * radius, c.y + std::sin(end_a) * radius};
    painter.FillRoundedRect({cap.x - 4.0f, cap.y - 4.0f, 8.0f, 8.0f}, 4.0f, arc);
    constexpr int kTicks = 9;
    for (int i = 0; i <= kTicks; ++i) {
        const float a = (kStart + kSweep * (static_cast<float>(i) / kTicks)) * 3.14159265f / 180.0f;
        const float cs = std::cos(a);
        const float sn = std::sin(a);
        const Point a0{c.x + cs * (radius - 8.0f), c.y + sn * (radius - 8.0f)};
        const Point a1{c.x + cs * (radius + 1.0f), c.y + sn * (radius + 1.0f)};
        painter.DrawLine(a0, a1, theme.stroke_divider, 1.0f);
    }
    wchar_t num[24];
    chart_geom::FormatValue(value_, num, 24);
    if (!unit_.empty()) {
        painter.DrawText(num, {absolute_.x, c.y - 18.0f, absolute_.w, 22.0f}, TextRole::Title,
                         theme.text, Align::Center);
        painter.DrawText(unit_, {absolute_.x, c.y + 4.0f, absolute_.w, 16.0f}, TextRole::Caption,
                         theme.text_secondary, Align::Center);
    } else {
        painter.DrawText(num, {absolute_.x, c.y - 12.0f, absolute_.w, 22.0f}, TextRole::Title,
                         theme.text, Align::Center);
    }
    if (!caption_.empty()) {
        painter.DrawText(caption_, {absolute_.x, c.y + (unit_.empty() ? 12.0f : 20.0f), absolute_.w, 18.0f},
                         TextRole::Caption, theme.text_secondary, Align::Center);
    }
}

} // namespace lumen
