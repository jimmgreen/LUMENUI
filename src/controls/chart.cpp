#include "lumen/Chart.h"
#include "lumen/Painter.h"
#include "../core/chart_geom.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace lumen {
namespace {

using chart_geom::kMax;
using chart_geom::kSplineMax;

constexpr float kPi = 3.14159265f;
constexpr size_t kMaxBars = 8;

void FillArea(Painter& painter, const Point* pts, size_t n, const Rect& box, Color fill) {
    if (n < 2) return;
    for (size_t i = 1; i < n; ++i) {
        painter.FillTriangle(pts[i - 1], pts[i], {pts[i].x, box.Bottom()}, fill);
        painter.FillTriangle(pts[i - 1], {pts[i].x, box.Bottom()}, {pts[i - 1].x, box.Bottom()},
                             fill);
    }
}

void DrawPeak(Painter& painter, const Theme& theme, const Point* pts, size_t n, const float* ys,
              size_t yn) {
    if (n == 0 || yn == 0) return;
    size_t peak = 0;
    for (size_t i = 1; i < yn; ++i) {
        if (ys[i] > ys[peak]) peak = i;
    }
    const float t = yn <= 1 ? 0.0f : static_cast<float>(peak) / static_cast<float>(yn - 1);
    const size_t pi = n <= 1 ? 0 : static_cast<size_t>(t * static_cast<float>(n - 1) + 0.5f);
    const Point p = pts[std::min(pi, n - 1)];
    painter.FillRoundedRect({p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f}, 2.5f, theme.glow_sm);
}

void DrawXLabels(Painter& painter, const Theme& theme, const Rect& box, const std::wstring* labels,
                 size_t label_count, size_t hover_li) {
    if (label_count == 0) return;
    for (size_t i = 0; i < label_count; ++i) {
        const float t =
            label_count <= 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(label_count - 1);
        const float x = box.x + t * box.w;
        const Color ink = (i == hover_li) ? theme.text : theme.text_secondary;
        painter.DrawText(labels[i], {x - 28.0f, box.Bottom() + 3.0f, 56.0f, 14.0f},
                         TextRole::Caption, ink, Align::Center, 56.0f);
    }
}

void DrawSlotLabels(Painter& painter, const Theme& theme, const Rect& box, size_t n, size_t hover_i,
                    const std::wstring* const* names) {
    if (n == 0 || !names) return;
    const float slot = box.w / static_cast<float>(n);
    for (size_t i = 0; i < n; ++i) {
        if (!names[i] || names[i]->empty()) continue;
        const float x = box.x + slot * (static_cast<float>(i) + 0.5f);
        const Color ink = (i == hover_i) ? theme.text : theme.text_secondary;
        painter.DrawText(*names[i], {x - 28.0f, box.Bottom() + 3.0f, 56.0f, 14.0f},
                         TextRole::Caption, ink, Align::Center, 56.0f);
    }
}

}  // namespace

Chart& Chart::Count(size_t count) {
    count_ = count;
    Invalidate();
    return *this;
}

Chart& Chart::Values(std::function<float(size_t)> provider) {
    values_fn_ = std::move(provider);
    use_store_ = false;
    stored_ = 0;
    shown_ready_ = false;
    value_tween_.Snap(1.0f);
    Invalidate();
    return *this;
}

Chart& Chart::Values(std::span<const float> values) {
    const size_t old_n = (use_store_ && shown_ready_) ? stored_ : 0;
    float old_shown[kMaxSamples];
    if (old_n > 0) std::memcpy(old_shown, shown_, old_n * sizeof(float));
    stored_ = chart_geom::CopyEven(values_, kMaxSamples, values);
    count_ = values.size();
    use_store_ = stored_ > 0;
    values_fn_ = {};
    if (!use_store_) {
        shown_ready_ = false;
        value_tween_.Snap(1.0f);
    } else if (!shown_ready_ || MotionScale() <= 0.001f) {
        std::memcpy(shown_, values_, stored_ * sizeof(float));
        shown_ready_ = true;
        value_tween_.Snap(1.0f);
    } else {
        from_n_ = stored_;
        for (size_t i = 0; i < stored_; ++i) {
            from_[i] = (i < old_n) ? old_shown[i] : (old_n > 0 ? old_shown[old_n - 1] : 0.0f);
        }
        value_tween_.Play(0.0f, 1.0f, 0.24f, Ease::CssEaseOut);
        Animate();
    }
    Invalidate();
    return *this;
}

Chart& Chart::Targets(std::span<const float> values) {
    target_count_ = chart_geom::CopyEven(targets_, kMaxSamples, values);
    Invalidate();
    return *this;
}

Chart& Chart::Baseline(std::span<const float> values) {
    baseline_count_ = chart_geom::CopyEven(baseline_, kMaxSamples, values);
    Invalidate();
    return *this;
}

Chart& Chart::XLabels(std::initializer_list<std::wstring_view> labels) {
    label_count_ = 0;
    for (std::wstring_view s : labels) {
        if (label_count_ >= kMaxLabels) break;
        labels_[label_count_++] = s;
    }
    Invalidate();
    return *this;
}

Chart& Chart::Slices(std::initializer_list<ChartSlice> slices) {
    slice_count_ = 0;
    for (const ChartSlice& s : slices) {
        if (slice_count_ >= kMaxSlices) break;
        slices_[slice_count_].label = s.label;
        slices_[slice_count_].value = s.value;
        ++slice_count_;
    }
    Invalidate();
    return *this;
}

Chart& Chart::Grid(size_t cols, size_t rows) {
    grid_cols_ = std::min(cols, kMaxHeatCols);
    grid_rows_ = std::min(rows, kMaxHeatRows);
    RelayoutParent();
    return *this;
}

Chart& Chart::Cell(std::function<float(size_t, size_t)> provider) {
    cell_fn_ = std::move(provider);
    Invalidate();
    return *this;
}

bool Chart::SeriesVisible(size_t index) const noexcept {
    if (index >= kMaxSlices) return false;
    return !series_hidden_[index];
}

Chart& Chart::SeriesVisible(size_t index, bool on) {
    if (index >= kMaxSlices) return *this;
    series_hidden_[index] = !on;
    Invalidate();
    return *this;
}

size_t Chart::LegendCount() const noexcept {
    if (kind_ == ChartKind::Donut) return slice_count_;
    if (kind_ == ChartKind::Line || kind_ == ChartKind::Area) {
        if (baseline_count_ > 0) return 2;
        if (!series_name_.empty()) return 1;
    }
    return 0;
}

Rect Chart::LegendBounds(size_t index) const noexcept {
    if (index >= LegendCount()) return {};
    if (kind_ == ChartKind::Donut) {
        const Rect box = PlotBounds();
        return {box.Right() - 96.0f - absolute_.x,
                box.y + 4.0f + 18.0f * static_cast<float>(index) - absolute_.y, 96.0f, 18.0f};
    }
    const float ly = (!title_.empty() || !value_text_.empty()) ? 32.0f : 8.0f;
    constexpr float kItem = 96.0f;
    return {12.0f + static_cast<float>(index) * kItem, ly, kItem - 8.0f, 18.0f};
}

float Chart::DisplayValue(size_t index) const noexcept {
    const size_t src = use_store_ ? stored_ : count_;
    if (index >= src) return 0.0f;
    return DisplayAt(index, src);
}

Chart& Chart::ResetView() {
    view0_ = 0.0f;
    view1_ = 1.0f;
    panning_ = false;
    Invalidate();
    return *this;
}

bool Chart::Cartesian() const noexcept {
    return kind_ == ChartKind::Line || kind_ == ChartKind::Area || kind_ == ChartKind::Bar;
}

bool Chart::ShowsLegend() const noexcept {
    if (kind_ == ChartKind::Donut) return slice_count_ > 0;
    if (kind_ == ChartKind::Line || kind_ == ChartKind::Area) {
        return baseline_count_ > 0 || !series_name_.empty();
    }
    return false;
}

int Chart::LegendHit(Point local) const noexcept {
    const size_t n = LegendCount();
    for (size_t i = 0; i < n; ++i) {
        if (LegendBounds(i).Contains(local)) return static_cast<int>(i);
    }
    return -1;
}

float Chart::DisplayAt(size_t src_index, size_t src_count) const noexcept {
    (void)src_count;
    if (use_store_) {
        if (src_index >= stored_) return 0.0f;
        if (value_tween_.running) {
            const float t = value_tween_.Value();
            const float a = (src_index < from_n_) ? from_[src_index] : 0.0f;
            return a + (values_[src_index] - a) * t;
        }
        return shown_ready_ ? shown_[src_index] : values_[src_index];
    }
    return values_fn_ ? values_fn_(src_index) : 0.0f;
}

void Chart::ClampView() noexcept {
    const size_t src = use_store_ ? stored_ : count_;
    if (kind_ == ChartKind::Bar) {
        SnapBarView(src);
        return;
    }
    float min_span = 0.08f;
    if (src > 1) min_span = std::max(min_span, 3.0f / static_cast<float>(src));
    min_span = std::min(min_span, 1.0f);
    float span = view1_ - view0_;
    if (span < min_span) {
        const float mid = (view0_ + view1_) * 0.5f;
        view0_ = mid - min_span * 0.5f;
        view1_ = mid + min_span * 0.5f;
        span = min_span;
    }
    if (view0_ < 0.0f) {
        view1_ -= view0_;
        view0_ = 0.0f;
    }
    if (view1_ > 1.0f) {
        view0_ -= (view1_ - 1.0f);
        view1_ = 1.0f;
    }
    view0_ = Clamp(view0_, 0.0f, 1.0f);
    view1_ = Clamp(view1_, view0_, 1.0f);
    if (view1_ - view0_ < min_span) view1_ = std::min(1.0f, view0_ + min_span);
}

void Chart::SnapBarView(size_t src) noexcept {
    if (src <= 1) {
        view0_ = 0.0f;
        view1_ = 1.0f;
        return;
    }
    const float inv = 1.0f / static_cast<float>(src);
    const float min_span = std::min(1.0f, 2.0f * inv);
    if (view1_ - view0_ < min_span) {
        const float mid = (view0_ + view1_) * 0.5f;
        view0_ = mid - min_span * 0.5f;
        view1_ = mid + min_span * 0.5f;
    }
    if (view0_ < 0.0f) {
        view1_ -= view0_;
        view0_ = 0.0f;
    }
    if (view1_ > 1.0f) {
        view0_ -= (view1_ - 1.0f);
        view1_ = 1.0f;
    }
    view0_ = Clamp(view0_, 0.0f, 1.0f);
    view1_ = Clamp(view1_, view0_, 1.0f);
    size_t first = static_cast<size_t>(std::floor(view0_ * static_cast<float>(src) + 1.0e-4f));
    size_t last = static_cast<size_t>(std::ceil(view1_ * static_cast<float>(src) - 1.0e-4f));
    if (first >= src) first = src - 1;
    if (last <= first) last = first + 1;
    if (last > src) last = src;
    if (last - first < 2 && src >= 2) {
        if (last < src) ++last;
        else if (first > 0) --first;
    }
    view0_ = static_cast<float>(first) * inv;
    view1_ = static_cast<float>(last) * inv;
}

void Chart::BarWindow(size_t& first, size_t& n) const noexcept {
    const size_t src = use_store_ ? stored_ : count_;
    first = 0;
    n = 0;
    if (src == 0) return;
    first = static_cast<size_t>(std::floor(view0_ * static_cast<float>(src) + 1.0e-4f));
    size_t last = static_cast<size_t>(std::ceil(view1_ * static_cast<float>(src) - 1.0e-4f));
    if (first >= src) first = src - 1;
    if (last <= first) last = first + 1;
    if (last > src) last = src;
    n = last - first;
}

void Chart::ZoomAt(float pivot01, float factor) {
    const float p = Clamp(pivot01, 0.0f, 1.0f);
    const float span = std::max(1.0e-4f, view1_ - view0_);
    const float mid = view0_ + p * span;
    const float next = span * factor;
    view0_ = mid - p * next;
    view1_ = view0_ + next;
    ClampView();
    Invalidate();
}

void Chart::SampleSeries(float* ys, size_t& n, float& mn, float& mx, float width_dip,
                         bool normalize) const {
    const size_t src = use_store_ ? stored_ : count_;
    const bool vis0 = SeriesVisible(0);
    mn = 1.0e9f;
    mx = -1.0e9f;
    if (kind_ == ChartKind::Bar) {
        size_t first = 0;
        BarWindow(first, n);
        n = std::min(n, kMax);
        for (size_t i = 0; i < n; ++i) {
            const float v = DisplayAt(first + i, src);
            ys[i] = v;
            if (vis0) {
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
        }
    } else {
        size_t vis = src;
        if (src > 1 && (view0_ > 0.0f || view1_ < 1.0f)) {
            vis = std::max(size_t{2},
                           static_cast<size_t>((view1_ - view0_) * static_cast<float>(src) + 0.5f));
        }
        n = chart_geom::SampleCount(vis, width_dip);
        for (size_t i = 0; i < n; ++i) {
            const size_t si = chart_geom::WindowIndex(i, n, src, view0_, view1_);
            const float v = DisplayAt(si, src);
            ys[i] = v;
            if (vis0) {
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
        }
    }
    if (n == 0) {
        mn = 0.0f;
        mx = 1.0f;
        return;
    }
    if ((kind_ == ChartKind::Line || kind_ == ChartKind::Area) && baseline_count_ > 0 &&
        SeriesVisible(1)) {
        for (size_t i = 0; i < n; ++i) {
            const float v = baseline_[chart_geom::WindowIndex(i, n, baseline_count_, view0_, view1_)];
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
    }
    if (mx < mn) {
        mn = 0.0f;
        mx = 1.0f;
    }
    if (!normalize) return;
    if (kind_ == ChartKind::Line || kind_ == ChartKind::Area) {
        chart_geom::NiceAxis(mn, mx);
    } else {
        chart_geom::NormalizeRange(mn, mx);
    }
}

const std::wstring* Chart::LabelAt(size_t index, size_t count) const {
    if (label_count_ == 0 || count == 0) return nullptr;
    const size_t li = chart_geom::WindowIndex(index, count, label_count_, view0_, view1_);
    if (li >= label_count_) return nullptr;
    return &labels_[li];
}

Rect Chart::PlotBounds() const noexcept {
    float top = 10.0f;
    if (!title_.empty() || !value_text_.empty()) top = 40.0f;
    float bottom = 10.0f;
    if (label_count_ > 0) bottom += 18.0f;
    if (!hint_.empty()) bottom += 16.0f;
    float left = 12.0f;
    float right = 12.0f;
    if (kind_ == ChartKind::Line || kind_ == ChartKind::Area) {
        left = 40.0f;
        right = 14.0f;
        if (ShowsLegend()) top += 20.0f;
    } else if (kind_ == ChartKind::Radar) {
        left = 6.0f;
        right = 6.0f;
        top = std::max(top, 28.0f);
        bottom = std::max(bottom, 20.0f);
    }
    return {absolute_.x + left, absolute_.y + top, std::max(8.0f, absolute_.w - left - right),
            std::max(16.0f, absolute_.h - top - bottom)};
}

Size Chart::Measure(Size available, const Theme&) {
    float w = preferred_.w > 0.0f ? preferred_.w : 320.0f;
    if (available.w > 0.0f && available.w < 1.0e4f) w = available.w;
    float h = preferred_.h > 0.0f ? preferred_.h : 200.0f;
    if (kind_ == ChartKind::Heatmap && grid_rows_ > 0) {
        h = std::max(h, 40.0f + static_cast<float>(grid_rows_) * 16.0f);
    }
    return {w, h};
}

size_t Chart::HitIndex(Point local) const {
    if (kind_ != ChartKind::Funnel && kind_ != ChartKind::Bullet) {
        return static_cast<size_t>(-1);
    }
    const size_t src = use_store_ ? stored_ : count_;
    const size_t n = std::min(src, kMaxBars);
    if (n == 0) return static_cast<size_t>(-1);
    const Rect box = PlotBounds();
    const float ay = absolute_.y + local.y;
    const float ax = absolute_.x + local.x;
    if (ay < box.y || ay >= box.Bottom() || ax < box.x || ax >= box.Right()) {
        return static_cast<size_t>(-1);
    }
    const float slot = box.h / static_cast<float>(n);
    const size_t i = static_cast<size_t>((ay - box.y) / slot);
    return i >= n ? n - 1 : i;
}

void Chart::OnMouseMove(Point local, uint32_t buttons) {
    if (panning_ && (buttons & 0x0001) != 0 && Cartesian()) {
        const Rect box = PlotBounds();
        const float span = pan_v1_ - pan_v0_;
        const float dx = local.x - pan_local_.x;
        const float du = (box.w > 1.0f) ? (-dx / box.w) * span : 0.0f;
        view0_ = pan_v0_ + du;
        view1_ = pan_v1_ + du;
        ClampView();
        Invalidate();
        return;
    }
    const size_t prev = hover_i_;
    hover_i_ = HitIndex(local);
    if ((kind_ == ChartKind::Funnel || kind_ == ChartKind::Bullet) && hover_i_ != prev) {
        Animate();
    }
    Invalidate();
}

void Chart::OnMouseDown(Point local, uint32_t buttons) {
    if ((buttons & 0x0001) == 0) return;
    const int li = LegendHit(local);
    if (li >= 0) {
        SeriesVisible(static_cast<size_t>(li), !SeriesVisible(static_cast<size_t>(li)));
        return;
    }
    if (Cartesian()) {
        const Rect box = PlotBounds();
        if (box.Contains({absolute_.x + local.x, absolute_.y + local.y})) {
            panning_ = true;
            pan_local_ = local;
            pan_v0_ = view0_;
            pan_v1_ = view1_;
        }
    }
}

void Chart::OnMouseUp(Point local, uint32_t buttons) {
    (void)local;
    (void)buttons;
    panning_ = false;
}

void Chart::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_i_ = static_cast<size_t>(-1);
    if (kind_ == ChartKind::Funnel || kind_ == ChartKind::Bullet) Animate();
    Invalidate();
}

void Chart::OnMouseDoubleClick(Point local) {
    (void)local;
    if (Cartesian()) ResetView();
}

bool Chart::OnWheel(float delta) {
    if (!Cartesian()) return false;
    float pivot = 0.5f;
    const Rect box = PlotBounds();
    if (box.w > 1.0f) {
        pivot = Clamp((absolute_.x + mouse_local_.x - box.x) / box.w, 0.0f, 1.0f);
    }
    ZoomAt(pivot, delta > 0.0f ? 0.82f : 1.22f);
    return true;
}

CursorShape Chart::CursorAt(Point local) const {
    if (LegendHit(local) >= 0) return CursorShape::Hand;
    return CursorShape::Arrow;
}

bool Chart::OnAnimate(float dt_seconds) {
    bool more = Control::OnAnimate(dt_seconds);
    if (value_tween_.running) {
        const bool still = value_tween_.Tick(dt_seconds);
        const float t = value_tween_.Value();
        for (size_t i = 0; i < stored_; ++i) {
            const float a = (i < from_n_) ? from_[i] : 0.0f;
            shown_[i] = a + (values_[i] - a) * t;
        }
        more = still || more;
    }
    for (size_t i = 0; i < kMaxBars; ++i) {
        const float tgt = (hovered_ && hover_i_ == i) ? 1.0f : 0.0f;
        more = EaseTo(bar_scale_[i], tgt, dt_seconds, 18.0f) || more;
    }
    return more;
}

void Chart::DrawLegend(Painter& painter, const Theme& theme) const {
    if (kind_ != ChartKind::Line && kind_ != ChartKind::Area) return;
    const size_t n = LegendCount();
    for (size_t i = 0; i < n; ++i) {
        const Rect local = LegendBounds(i);
        const Rect r{absolute_.x + local.x, absolute_.y + local.y, local.w, local.h};
        const bool on = SeriesVisible(i);
        const Color ink = on ? (i == 0 ? theme.text : theme.text_secondary) : theme.text_disabled;
        painter.FillRoundedRect({r.x + 2.0f, r.y + 5.0f, 8.0f, 8.0f}, 4.0f, ink);
        std::wstring_view name = L"Value";
        if (i == 0) name = series_name_.empty() ? std::wstring_view(L"Active") : series_name_;
        else name = baseline_name_.empty() ? std::wstring_view(L"Baseline") : baseline_name_;
        painter.DrawText(name, {r.x + 14.0f, r.y, r.w - 16.0f, r.h}, TextRole::Caption, ink,
                         Align::Leading, r.w - 16.0f);
        if (!on) {
            painter.DrawLine({r.x + 14.0f, r.y + r.h * 0.5f}, {r.Right() - 6.0f, r.y + r.h * 0.5f},
                             theme.text_disabled, 1.0f);
        }
    }
}

void Chart::DrawCartesianHover(Painter& painter, const Theme& theme, const Rect& box,
                               const Point* src, const float* ys, size_t n, bool has_b,
                               const Point* bsrc, const float* bys) const {
    if (!hovered_ || n == 0) return;
    const bool vis0 = SeriesVisible(0);
    const bool vis1 = has_b && SeriesVisible(1);
    if (!vis0 && !vis1) return;
    const size_t hi = std::min(chart_geom::HoverIndex(box, absolute_.x + mouse_local_.x, n), n - 1);
    const Point anchor = vis0 ? src[hi] : bsrc[hi];
    chart_geom::DrawCrosshair(painter, theme, box, anchor);
    if (vis1) {
        chart_geom::FillDot(painter, bsrc[hi], 4.0f, theme.bg);
        chart_geom::StrokeDot(painter, bsrc[hi], 4.0f, theme.text, 1.2f);
    }
    if (vis0) {
        chart_geom::FillDot(painter, src[hi], 6.5f, theme.fill_selected);
        chart_geom::StrokeDot(painter, src[hi], 6.5f, theme.text_secondary, 1.5f);
        chart_geom::FillDot(painter, src[hi], 3.6f, theme.text);
    }
    wchar_t v0[16], v1[16];
    chart_geom::FormatValue(ys[hi], v0, 16);
    std::wstring_view title = L"";
    if (const std::wstring* lab = LabelAt(hi, n)) title = *lab;
    if (title.empty()) title = v0;
    const std::wstring_view a_name =
        series_name_.empty() ? std::wstring_view(L"Value") : series_name_;
    const std::wstring_view b_name =
        baseline_name_.empty() ? std::wstring_view(L"Baseline") : baseline_name_;
    const Rect host{absolute_.x, absolute_.y, absolute_.w, absolute_.h};
    if (vis0 && vis1) {
        chart_geom::FormatValue(bys[hi], v1, 16);
        chart_geom::DrawCallout(painter, theme, anchor, host, title, b_name, v1, theme.text_secondary,
                                a_name, v0, theme.text);
    } else if (vis0) {
        chart_geom::DrawCallout(painter, theme, anchor, host, title, a_name, v0, theme.text);
    } else {
        chart_geom::FormatValue(bys[hi], v1, 16);
        chart_geom::DrawCallout(painter, theme, anchor, host, title, b_name, v1, theme.text_secondary);
    }
}

void Chart::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    const float x = absolute_.x;
    const float w = absolute_.w;
    if (!title_.empty() || !value_text_.empty()) {
        painter.DrawText(title_, {x + 12.0f, absolute_.y + 10.0f, w * 0.55f, 20.0f},
                         TextRole::Caption, theme.text_secondary, Align::Leading, w * 0.55f);
        painter.DrawText(value_text_, {x + w * 0.38f, absolute_.y + 8.0f, w * 0.58f - 12.0f, 24.0f},
                         TextRole::Title, theme.text, Align::Trailing, w * 0.55f);
    }
    if (!hint_.empty()) {
        painter.DrawText(hint_, {x + 12.0f, absolute_.Bottom() - 18.0f, w - 24.0f, 16.0f},
                         TextRole::Caption, theme.text_secondary, Align::Leading, w - 24.0f);
    }
    DrawLegend(painter, theme);

    const Rect box = PlotBounds();
    if (box.IsEmpty()) return;

    switch (kind_) {
    case ChartKind::Line:
    case ChartKind::Area: {
        float ys[kMax];
        size_t n = 0;
        float mn = 0.0f, mx = 1.0f;
        SampleSeries(ys, n, mn, mx, box.w);
        if (n == 0) return;
        chart_geom::DrawDashGrid(painter, theme, box, mn, mx);
        Point src[kMax];
        for (size_t i = 0; i < n; ++i) src[i] = chart_geom::MapX(box, i, n, ys[i], mn, mx);
        Point spline[kSplineMax];
        const size_t sn = chart_geom::ExpandSpline(src, n, spline);

        const bool has_b = baseline_count_ > 0;
        Point bsrc[kMax]{};
        Point bspline[kSplineMax];
        size_t bsn = 0;
        float bys[kMax]{};
        if (has_b && SeriesVisible(1)) {
            for (size_t i = 0; i < n; ++i) {
                bys[i] = baseline_[chart_geom::WindowIndex(i, n, baseline_count_, view0_, view1_)];
                bsrc[i] = chart_geom::MapX(box, i, n, bys[i], mn, mx);
            }
            bsn = chart_geom::ExpandSpline(bsrc, n, bspline);
            painter.StrokeOpenPolyline(bspline, static_cast<int>(bsn), theme.text_secondary, 1.2f,
                                       true);
        }

        if (SeriesVisible(0)) {
            if (kind_ == ChartKind::Area) FillArea(painter, spline, sn, box, theme.fill_hover);
            painter.StrokeOpenPolyline(spline, static_cast<int>(sn), theme.text, has_b ? 2.2f : 1.8f);
            if (!has_b) DrawPeak(painter, theme, spline, sn, ys, n);
            for (size_t i = 0; i < n; ++i) {
                chart_geom::FillDot(painter, src[i], 2.6f, theme.text);
            }
        }

        size_t hover_li = static_cast<size_t>(-1);
        if (hovered_ && n > 0 && label_count_ > 0) {
            const size_t hi = std::min(chart_geom::HoverIndex(box, absolute_.x + mouse_local_.x, n),
                                       n - 1);
            hover_li = chart_geom::WindowIndex(hi, n, label_count_, view0_, view1_);
        }
        DrawXLabels(painter, theme, box, labels_, label_count_, hover_li);
        if (hovered_ && n > 0) {
            DrawCartesianHover(painter, theme, box, src, ys, n, has_b && SeriesVisible(1), bsrc, bys);
        }
        break;
    }
    case ChartKind::Bar: {
        float ys[kMax];
        size_t n = 0;
        float mn = 0.0f, mx = 1.0f;
        SampleSeries(ys, n, mn, mx, box.w);
        if (n == 0) return;
        chart_geom::DrawGrid(painter, theme, box);
        const bool horiz = bar_ == ChartBar::Horizontal;
        const float span = std::max(1.0e-6f, mx - std::min(0.0f, mn));
        const bool vis0 = SeriesVisible(0);
        size_t hi = static_cast<size_t>(-1);
        if (hovered_ && n > 0) {
            if (horiz) {
                const float t = Clamp(mouse_local_.y / std::max(1.0f, absolute_.h), 0.0f, 0.999f);
                hi = static_cast<size_t>(t * static_cast<float>(n));
            } else {
                hi = std::min(chart_geom::HoverIndex(box, absolute_.x + mouse_local_.x, n), n - 1);
            }
        }
        if (vis0) {
            if (horiz) {
                const float slot = box.h / static_cast<float>(n);
                const float bh = std::max(4.0f, slot * 0.62f);
                for (size_t i = 0; i < n; ++i) {
                    const float t = Clamp(ys[i] / span, 0.0f, 1.0f);
                    const float bw = std::max(4.0f, t * box.w);
                    const float y = box.y + slot * static_cast<float>(i) + (slot - bh) * 0.5f;
                    const Color fill = (i == hi) ? theme.text : theme.text_secondary;
                    painter.FillRoundedRect({box.x, y, bw, bh}, bh * 0.5f, fill);
                }
            } else {
                const float slot = box.w / static_cast<float>(n);
                const float bw = std::max(3.0f, slot * 0.62f);
                for (size_t i = 0; i < n; ++i) {
                    const float t = Clamp((ys[i] - mn) / std::max(1.0e-6f, mx - mn), 0.0f, 1.0f);
                    const float h = std::max(4.0f, t * box.h);
                    const float px = box.x + slot * static_cast<float>(i) + (slot - bw) * 0.5f;
                    const Color fill = (i == hi) ? theme.text : theme.text_secondary;
                    painter.FillRoundedRect({px, box.Bottom() - h, bw, h}, std::min(bw, h) * 0.5f,
                                            fill);
                }
            }
        }
        const std::wstring* names[kMax]{};
        size_t first = 0;
        size_t bar_n = 0;
        BarWindow(first, bar_n);
        (void)bar_n;
        for (size_t i = 0; i < n; ++i) {
            const size_t si = first + i;
            names[i] = (si < label_count_) ? &labels_[si] : nullptr;
        }
        if (!horiz) DrawSlotLabels(painter, theme, box, n, hi, names);
        else DrawXLabels(painter, theme, box, labels_, label_count_, hi);
        if (hovered_ && vis0 && n > 0 && hi < n) {
            wchar_t buf[24];
            chart_geom::FormatValue(ys[hi], buf, 24);
            Point anchor{box.x + box.w * 0.5f, box.y};
            if (horiz) {
                const float slot = box.h / static_cast<float>(n);
                const float t = Clamp(ys[hi] / span, 0.0f, 1.0f);
                const float bw = std::max(4.0f, t * box.w);
                anchor = {box.x + bw, box.y + slot * (static_cast<float>(hi) + 0.5f)};
            } else {
                const float slot = box.w / static_cast<float>(n);
                const float t = Clamp((ys[hi] - mn) / std::max(1.0e-6f, mx - mn), 0.0f, 1.0f);
                const float h = std::max(4.0f, t * box.h);
                anchor = {box.x + slot * (static_cast<float>(hi) + 0.5f), box.Bottom() - h};
            }
            std::wstring_view title = L"";
            if (names[hi]) title = *names[hi];
            if (title.empty()) title = buf;
            const std::wstring_view a_name =
                series_name_.empty() ? std::wstring_view(L"Value") : series_name_;
            const Rect host{absolute_.x, absolute_.y, absolute_.w, absolute_.h};
            chart_geom::DrawCallout(painter, theme, anchor, host, title, a_name, buf, theme.text);
        }
        break;
    }
    case ChartKind::Donut: {
        float sum = 0.0f;
        for (size_t i = 0; i < slice_count_; ++i) {
            if (SeriesVisible(i)) sum += std::max(0.0f, slices_[i].value);
        }
        if (sum < 1.0e-6f || slice_count_ == 0) {
            float ly = box.y + 4.0f;
            for (size_t i = 0; i < slice_count_; ++i) {
                const Color ink = theme.text_disabled;
                painter.FillRoundedRect({box.Right() - 96.0f, ly + 4.0f, 8.0f, 8.0f}, 4.0f, ink);
                painter.DrawText(slices_[i].label, {box.Right() - 84.0f, ly, 80.0f, 16.0f},
                                 TextRole::Caption, ink, Align::Leading, 80.0f);
                painter.DrawLine({box.Right() - 84.0f, ly + 8.0f}, {box.Right() - 8.0f, ly + 8.0f},
                                 ink, 1.0f);
                ly += 18.0f;
            }
            break;
        }
        const Point c{box.x + box.w * 0.38f, box.y + box.h * 0.5f};
        const float radius = std::min(box.w * 0.32f, box.h * 0.42f);
        constexpr float kGap = 4.0f;
        float ang = -90.0f;
        for (size_t i = 0; i < slice_count_; ++i) {
            if (!SeriesVisible(i)) continue;
            const float frac = std::max(0.0f, slices_[i].value) / sum;
            const float sweep = frac * 360.0f - kGap;
            if (sweep > 0.5f) {
                const float t = slice_count_ <= 1
                                    ? 1.0f
                                    : 1.0f - static_cast<float>(i) / static_cast<float>(slice_count_ - 1);
                painter.DrawArc(c, radius, ang + kGap * 0.5f, sweep, chart_geom::Tone(theme, t),
                                14.0f);
            }
            ang += frac * 360.0f;
        }
        const std::wstring_view center = value_text_.empty() ? std::wstring_view(L"100%")
                                                             : std::wstring_view(value_text_);
        painter.DrawText(center, {c.x - 40.0f, c.y - 12.0f, 80.0f, 24.0f}, TextRole::Title,
                         theme.text, Align::Center);
        float ly = box.y + 4.0f;
        for (size_t i = 0; i < slice_count_; ++i) {
            const float t = slice_count_ <= 1
                                ? 1.0f
                                : 1.0f - static_cast<float>(i) / static_cast<float>(slice_count_ - 1);
            const bool on = SeriesVisible(i);
            const Color ink = on ? theme.text : theme.text_disabled;
            painter.FillRoundedRect({box.Right() - 96.0f, ly + 4.0f, 8.0f, 8.0f}, 4.0f,
                                    on ? chart_geom::Tone(theme, t) : theme.text_disabled);
            painter.DrawText(slices_[i].label, {box.Right() - 84.0f, ly, 80.0f, 16.0f},
                             TextRole::Caption, ink, Align::Leading, 80.0f);
            if (!on) {
                painter.DrawLine({box.Right() - 84.0f, ly + 8.0f}, {box.Right() - 8.0f, ly + 8.0f},
                                 theme.text_disabled, 1.0f);
            }
            ly += 18.0f;
        }
        break;
    }
    case ChartKind::Heatmap: {
        if (grid_cols_ == 0 || grid_rows_ == 0 || !cell_fn_) return;
        const float gap = 3.0f;
        const float cw = (box.w - gap * static_cast<float>(grid_cols_ - 1)) /
                         static_cast<float>(grid_cols_);
        const float rh = (box.h - gap * static_cast<float>(grid_rows_ - 1)) /
                         static_cast<float>(grid_rows_);
        const float cell = std::max(4.0f, std::min(cw, rh));
        float mn = 1.0e9f, mx = -1.0e9f;
        float cells[kMaxHeatCols * kMaxHeatRows];
        const size_t total = grid_cols_ * grid_rows_;
        for (size_t i = 0; i < total; ++i) {
            const size_t x_i = i % grid_cols_;
            const size_t y_i = i / grid_cols_;
            const float v = cell_fn_(x_i, y_i);
            cells[i] = v;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        chart_geom::NormalizeRange(mn, mx);
        const float rad = std::min(4.0f, cell * 0.35f);
        size_t hx = static_cast<size_t>(-1);
        size_t hy = static_cast<size_t>(-1);
        if (hovered_) {
            const float mxpos = absolute_.x + mouse_local_.x;
            const float mypos = absolute_.y + mouse_local_.y;
            for (size_t y_i = 0; y_i < grid_rows_; ++y_i) {
                for (size_t x_i = 0; x_i < grid_cols_; ++x_i) {
                    const Rect r{box.x + static_cast<float>(x_i) * (cell + gap),
                                 box.y + static_cast<float>(y_i) * (cell + gap), cell, cell};
                    if (r.Contains(mxpos, mypos)) {
                        hx = x_i;
                        hy = y_i;
                    }
                }
            }
        }
        for (size_t y_i = 0; y_i < grid_rows_; ++y_i) {
            for (size_t x_i = 0; x_i < grid_cols_; ++x_i) {
                const float v = cells[y_i * grid_cols_ + x_i];
                const float t = (v - mn) / (mx - mn);
                const Rect r{box.x + static_cast<float>(x_i) * (cell + gap),
                             box.y + static_cast<float>(y_i) * (cell + gap), cell, cell};
                painter.FillRoundedRect(r, rad, chart_geom::Heat(theme, t));
            }
        }
        if (hx < grid_cols_ && hy < grid_rows_) {
            const float v = cells[hy * grid_cols_ + hx];
            const Rect r{box.x + static_cast<float>(hx) * (cell + gap),
                         box.y + static_cast<float>(hy) * (cell + gap), cell, cell};
            painter.StrokeRoundedRect(r, rad, theme.text, 1.2f);
            wchar_t buf[24];
            chart_geom::FormatValue(v, buf, 24);
            wchar_t title[24];
            std::swprintf(title, 24, L"%zu, %zu", hx, hy);
            const Rect host{absolute_.x, absolute_.y, absolute_.w, absolute_.h};
            chart_geom::DrawCallout(painter, theme, {r.x + r.w * 0.5f, r.y}, host, title, L"Value",
                                    buf, theme.text);
        }
        break;
    }
    case ChartKind::Radar: {
        float ys[kMax];
        size_t n = 0;
        float mn = 0.0f, mx = 1.0f;
        SampleSeries(ys, n, mn, mx, 64.0f, false);
        if (n < 3) return;
        n = std::min(n, kMaxBars);
        const Point c{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
        const float radius = std::min(box.w, box.h) * 0.32f;
        auto vertex = [&](size_t i, float t) -> Point {
            const float a = -kPi * 0.5f + kPi * 2.0f * static_cast<float>(i) / static_cast<float>(n);
            return {c.x + std::cos(a) * radius * t, c.y + std::sin(a) * radius * t};
        };
        const float vmax = mx > 1.001f ? std::max(mx, 1.0e-6f) : 1.0f;
        (void)mn;
        auto unit = [&](float y) -> float { return Clamp(y / vmax, 0.0f, 1.0f); };

        Point ring[9];
        for (int ring_i = 1; ring_i <= 4; ++ring_i) {
            const float t = static_cast<float>(ring_i) / 4.0f;
            for (size_t i = 0; i < n; ++i) ring[i] = vertex(i, t);
            ring[n] = ring[0];
            painter.StrokeOpenPolyline(ring, static_cast<int>(n + 1), theme.stroke_divider, 1.0f);
        }
        for (size_t i = 0; i < n; ++i) {
            painter.DrawLine(c, vertex(i, 1.0f), theme.stroke_divider, 1.0f);
        }

        Point poly[9];
        for (size_t i = 0; i < n; ++i) poly[i] = vertex(i, unit(ys[i]));
        for (size_t i = 1; i + 1 < n; ++i) {
            painter.FillTriangle(poly[0], poly[i], poly[i + 1], theme.fill_selected);
        }
        poly[n] = poly[0];
        painter.StrokeOpenPolyline(poly, static_cast<int>(n + 1), theme.text, 2.2f);

        for (size_t i = 0; i < n; ++i) {
            const float a = -kPi * 0.5f + kPi * 2.0f * static_cast<float>(i) / static_cast<float>(n);
            const Point lp = vertex(i, 1.22f);
            if (const std::wstring* lab = LabelAt(i, n)) {
                float lx = lp.x - 32.0f;
                Align align = Align::Center;
                if (std::cos(a) > 0.35f) {
                    align = Align::Leading;
                    lx = lp.x + 6.0f;
                } else if (std::cos(a) < -0.35f) {
                    align = Align::Trailing;
                    lx = lp.x - 70.0f;
                }
                float ly = lp.y - 8.0f;
                if (std::sin(a) < -0.5f) ly = lp.y - 18.0f;
                if (std::sin(a) > 0.5f) ly = lp.y + 4.0f;
                painter.DrawText(*lab, {lx, ly, 64.0f, 14.0f}, TextRole::Caption,
                                 theme.text_secondary, align, 64.0f);
            }
        }

        if (hovered_ && n > 0) {
            const float mouse_x = absolute_.x + mouse_local_.x;
            const float mouse_y = absolute_.y + mouse_local_.y;
            const float dx = mouse_x - c.x;
            const float dy = mouse_y - c.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < radius * 1.25f && dist > 4.0f) {
                const float mouse_a = std::atan2(dy, dx);
                size_t hi = 0;
                float best = 1.0e9f;
                for (size_t i = 0; i < n; ++i) {
                    const float a =
                        -kPi * 0.5f + kPi * 2.0f * static_cast<float>(i) / static_cast<float>(n);
                    const float da = std::fabs(std::atan2(std::sin(mouse_a - a),
                                                          std::cos(mouse_a - a)));
                    if (da < best) {
                        best = da;
                        hi = i;
                    }
                }
                if (best <= kPi / static_cast<float>(n) + 0.12f) {
                    painter.DrawLine(c, poly[hi], theme.text, 1.0f);
                    chart_geom::FillDot(painter, poly[hi], 6.0f, theme.text);
                    wchar_t val[16];
                    std::swprintf(val, 16, L"%d", chart_geom::AsPercent(ys[hi]));
                    std::wstring_view title = L"Axis";
                    if (const std::wstring* lab = LabelAt(hi, n)) title = *lab;
                    const Rect host{absolute_.x, absolute_.y, absolute_.w, absolute_.h};
                    chart_geom::DrawCallout(painter, theme, poly[hi], host, title, L"Metric", val,
                                            theme.text);
                }
            }
        }
        break;
    }
    case ChartKind::Funnel: {
        float ys[kMax];
        size_t n = 0;
        float mn = 0.0f, mx = 1.0f;
        SampleSeries(ys, n, mn, mx, box.w);
        if (n == 0) return;
        n = std::min(n, kMaxBars);
        (void)mn;
        (void)mx;
        const float slot = box.h / static_cast<float>(n);
        const float bh0 = std::max(12.0f, slot * 0.72f);
        float vmax = ys[0];
        for (size_t i = 1; i < n; ++i) vmax = std::max(vmax, ys[i]);
        vmax = std::max(vmax, 1.0e-6f);
        for (size_t i = 0; i < n; ++i) {
            const float t = Clamp(ys[i] / vmax, 0.28f, 1.0f);
            const float sc = 1.0f + 0.06f * bar_scale_[i];
            const float bw = std::max(36.0f, t * box.w) * sc;
            const float bh = bh0 * sc;
            const float y = box.y + slot * static_cast<float>(i) + (slot - bh) * 0.5f;
            const float px = box.x + (box.w - bw) * 0.5f;
            const float shade =
                n <= 1 ? 1.0f
                       : 1.0f - 0.78f * static_cast<float>(i) / static_cast<float>(n - 1);
            const Color fill = chart_geom::Tone(theme, shade);
            const Color hi{std::min(1.0f, fill.r + 0.10f), std::min(1.0f, fill.g + 0.10f),
                           std::min(1.0f, fill.b + 0.10f), 1.0f};
            const Rect bar{px, y, bw, bh};
            painter.FillRoundedRectLinear(bar, bh * 0.5f, hi, fill);
            if (const std::wstring* lab = LabelAt(i, n)) {
                painter.DrawText(*lab, bar, TextRole::CaptionStrong,
                                 chart_geom::InkOnTone(theme, shade), Align::Center, bw);
            }
        }
        break;
    }
    case ChartKind::Bullet: {
        float ys[kMax];
        size_t n = 0;
        float mn = 0.0f, mx = 1.0f;
        SampleSeries(ys, n, mn, mx, box.w, false);
        if (n == 0) return;
        n = std::min(n, kMaxBars);
        (void)mn;
        (void)mx;
        const float slot = box.h / static_cast<float>(n);
        const float label_w = std::min(96.0f, box.w * 0.28f);
        const float value_w = 72.0f;
        const float tw = std::max(24.0f, box.w - label_w - value_w - 8.0f);
        const float tx = box.x + label_w;
        for (size_t i = 0; i < n; ++i) {
            const float sc = 1.0f + 0.06f * bar_scale_[i];
            const float y = box.y + slot * static_cast<float>(i);
            if (const std::wstring* lab = LabelAt(i, n)) {
                painter.DrawText(*lab, {box.x, y + 2.0f, label_w - 6.0f, 16.0f}, TextRole::Caption,
                                 theme.text_secondary, Align::Leading, label_w - 6.0f);
            }
            const float tgt = i < target_count_ ? targets_[i] : 0.0f;
            wchar_t pair[24];
            std::swprintf(pair, 24, L"%d%% / %d%%", chart_geom::AsPercent(ys[i]),
                          chart_geom::AsPercent(tgt));
            painter.DrawText(pair, {box.Right() - value_w, y + 2.0f, value_w, 16.0f},
                             TextRole::Caption, theme.text, Align::Trailing, value_w);

            const float track_h = 12.0f * sc;
            const float track_w = tw * sc;
            const float txx = tx + (tw - track_w) * 0.5f;
            const float ty = y + (slot - track_h) * 0.62f;
            painter.FillRoundedRect({txx, ty, track_w, track_h}, track_h * 0.5f,
                                    theme.fill_input_pressed);
            const float actual = Clamp(ys[i] > 1.001f ? ys[i] / 100.0f : ys[i], 0.0f, 1.0f);
            painter.FillRoundedRect({txx, ty, std::max(8.0f, track_w * actual), track_h},
                                    track_h * 0.5f, theme.text);
            if (i < target_count_) {
                const float tg = Clamp(tgt > 1.001f ? tgt / 100.0f : tgt, 0.0f, 1.0f);
                const float mxpos = txx + track_w * tg;
                const bool on_fill = tg <= actual + 0.002f;
                painter.FillRect({mxpos - 1.5f, ty - 3.0f, 3.0f, track_h + 6.0f},
                                 on_fill ? theme.bg : theme.text);
            }
        }
        break;
    }
    }
}

}  // namespace lumen
