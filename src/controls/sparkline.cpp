#include "lumen/Sparkline.h"
#include "lumen/Painter.h"
#include "../core/chart_geom.h"
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {
using chart_geom::kMax;
using chart_geom::kSplineMax;
}  // namespace

Sparkline& Sparkline::Values(std::span<const float> values) {
    stored_ = chart_geom::CopyEven(stored_values_, kMaxSamples, values);
    count_ = values.size();
    use_store_ = stored_ > 0;
    values_ = {};
    Invalidate();
    return *this;
}

Size Sparkline::Measure(Size available, const Theme&) {
    const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 160.0f;
    return {w, 36.0f};
}

void Sparkline::OnMouseMove(Point, uint32_t) { Invalidate(); }

void Sparkline::OnMouseLeave() {
    Control::OnMouseLeave();
    Invalidate();
}

void Sparkline::Draw(Painter& painter, const Theme& theme) {
    const size_t src = use_store_ ? stored_ : count_;
    if (src == 0 || (!use_store_ && !values_) || absolute_.w < 2.0f || absolute_.h < 2.0f) return;
    const Rect box = absolute_.Inset(2.0f, 4.0f);
    const size_t samples = chart_geom::SampleCount(src, box.w);
    float ys[kMax];
    float mn = 1.0e9f;
    float mx = -1.0e9f;
    for (size_t i = 0; i < samples; ++i) {
        const size_t si = chart_geom::SourceIndex(i, samples, src);
        const float v = use_store_ ? stored_values_[si] : values_(si);
        ys[i] = v;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    chart_geom::NormalizeRange(mn, mx);
    auto pt = [&](size_t i) -> Point { return chart_geom::MapX(box, i, samples, ys[i], mn, mx); };
    if (style_ == SparklineStyle::Bar) {
        const float slot = box.w / static_cast<float>(samples);
        const float bar_w = std::max(2.0f, slot * 0.62f);
        for (size_t i = 0; i < samples; ++i) {
            const Point p = pt(i);
            const float h = std::max(2.0f, box.Bottom() - p.y);
            painter.FillRoundedRect({p.x - bar_w * 0.5f, p.y, bar_w, h},
                                    std::min(bar_w, h) * 0.5f, theme.text_secondary);
        }
    } else {
        Point src_pts[kMax];
        for (size_t i = 0; i < samples; ++i) src_pts[i] = pt(i);
        Point spline[kSplineMax];
        const size_t sn = chart_geom::ExpandSpline(src_pts, samples, spline);
        if (style_ == SparklineStyle::Area) {
            for (size_t i = 1; i < sn; ++i) {
                painter.FillTriangle(spline[i - 1], spline[i], {spline[i].x, box.Bottom()},
                                     theme.fill_hover);
                painter.FillTriangle(spline[i - 1], {spline[i].x, box.Bottom()},
                                     {spline[i - 1].x, box.Bottom()}, theme.fill_hover);
            }
        }
        painter.StrokeOpenPolyline(spline, static_cast<int>(sn), theme.text, 1.4f);
        size_t peak_i = 0;
        for (size_t i = 1; i < samples; ++i) {
            if (ys[i] > ys[peak_i]) peak_i = i;
        }
        const Point peak_pt = pt(peak_i);
        painter.FillRoundedRect({peak_pt.x - 2.0f, peak_pt.y - 2.0f, 4.0f, 4.0f}, 2.0f,
                                theme.glow_sm);
    }
    if (hovered_ && samples > 0) {
        const size_t i =
            std::min(chart_geom::HoverIndex(box, absolute_.x + mouse_local_.x, samples),
                     samples - 1);
        chart_geom::DrawHover(painter, theme, box, pt(i), ys[i]);
    }
}

} // namespace lumen
