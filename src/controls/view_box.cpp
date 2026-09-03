#include "lumen/Viewbox.h"
#include "lumen/Painter.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lumen {
namespace {

bool AxisFinite(float v) { return v >= 0.0f && v < 1.0e4f; }

float AxisAlign(Align align, float extra) {
    switch (align) {
    case Align::Trailing:
        return extra;
    case Align::Center:
        return extra * 0.5f;
    default:
        return 0.0f;
    }
}

} // namespace

Viewbox& Viewbox::Stretch(ViewboxStretch value) {
    if (stretch_ == value) return *this;
    stretch_ = value;
    Relayout();
    return *this;
}

Viewbox& Viewbox::HorizontalAlignment(Align value) {
    if (align_x_ == value) return *this;
    align_x_ = value;
    Relayout();
    return *this;
}

Viewbox& Viewbox::VerticalAlignment(Align value) {
    if (align_y_ == value) return *this;
    align_y_ = value;
    Relayout();
    return *this;
}

size_t Viewbox::ContentIndex() const {
    for (size_t i = 0; i < children_.size(); ++i) {
        if (ChildVisible(i)) return i;
    }
    return static_cast<size_t>(-1);
}

void Viewbox::ComputeScale(Size natural, Size viewport, float& sx, float& sy) const {
    sx = 1.0f;
    sy = 1.0f;
    if (natural.w < 0.5f || natural.h < 0.5f) return;
    const bool fw = AxisFinite(viewport.w);
    const bool fh = AxisFinite(viewport.h);
    switch (stretch_) {
    case ViewboxStretch::None:
        return;
    case ViewboxStretch::Fill:
        if (fw) sx = viewport.w / natural.w;
        if (fh) sy = viewport.h / natural.h;
        return;
    case ViewboxStretch::Uniform:
    case ViewboxStretch::UniformToFill: {
        float s = 1.0f;
        if (fw && fh) {
            const float sxn = viewport.w / natural.w;
            const float syn = viewport.h / natural.h;
            s = (stretch_ == ViewboxStretch::Uniform) ? std::min(sxn, syn) : std::max(sxn, syn);
        } else if (fw) {
            s = viewport.w / natural.w;
        } else if (fh) {
            s = viewport.h / natural.h;
        }
        sx = sy = s;
        return;
    }
    }
}

Point Viewbox::MapToChildren(Point window_dip) const {
    if (sx_ < 1.0e-6f || sy_ < 1.0e-6f) return window_dip;
    return {origin_.x + (window_dip.x - origin_.x) / sx_,
            origin_.y + (window_dip.y - origin_.y) / sy_};
}

Rect Viewbox::MapClipToChildren(const Rect& clip) const {
    if (clip.IsEmpty() || sx_ < 1.0e-6f || sy_ < 1.0e-6f) return clip;
    const Point a = MapToChildren({clip.x, clip.y});
    const Point b = MapToChildren({clip.Right(), clip.Bottom()});
    const float x = std::min(a.x, b.x);
    const float y = std::min(a.y, b.y);
    return {x, y, std::fabs(b.x - a.x), std::fabs(b.y - a.y)};
}

void Viewbox::PushChildDraw(Painter& painter) const {
    // 先按窗口 DIP 用 Layer 裁视口，再缩放。轴对齐裁剪跟非等比变换叠用会漏或裁空。
    painter.PushRectClip(absolute_);
    painter.PushScale(origin_, sx_, sy_);
}

void Viewbox::PopChildDraw(Painter& painter) const {
    painter.PopTransform();
    painter.PopRectClip();
}

void Viewbox::Prepare(Painter& painter) {
    painter.PrepareRectClip(absolute_);
}

Size Viewbox::Measure(Size available, const Theme& theme) {
    const size_t index = ContentIndex();
    if (index == static_cast<size_t>(-1)) return {};
    const Size natural = MeasureChildAt(index, {1.0e5f, 1.0e5f}, theme);
    float sx = 1.0f, sy = 1.0f;
    ComputeScale(natural, available, sx, sy);
    float out_w = natural.w * sx;
    float out_h = natural.h * sy;
    if (stretch_ == ViewboxStretch::Fill || stretch_ == ViewboxStretch::UniformToFill) {
        if (AxisFinite(available.w)) out_w = available.w;
        if (AxisFinite(available.h)) out_h = available.h;
    } else if (stretch_ == ViewboxStretch::None) {
        if (AxisFinite(available.w)) out_w = std::min(natural.w, available.w);
        if (AxisFinite(available.h)) out_h = std::min(natural.h, available.h);
    }
    return {out_w, out_h};
}

void Viewbox::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const size_t index = ContentIndex();
    if (index == static_cast<size_t>(-1)) {
        origin_ = {absolute.x, absolute.y};
        sx_ = sy_ = 1.0f;
        return;
    }
    const Size natural = ChildDesired(index);
    ComputeScale(natural, {absolute.w, absolute.h}, sx_, sy_);
    if (sx_ < 1.0e-6f) sx_ = 1.0e-6f;
    if (sy_ < 1.0e-6f) sy_ = 1.0e-6f;
    const float vis_w = natural.w * sx_;
    const float vis_h = natural.h * sy_;
    origin_ = {absolute.x + AxisAlign(align_x_, absolute.w - vis_w),
               absolute.y + AxisAlign(align_y_, absolute.h - vis_h)};
    SetChildBounds(Child(index),
                   {origin_.x - absolute.x, origin_.y - absolute.y, natural.w, natural.h});
    ArrangeChildAt(index);
}

void Viewbox::Draw(Painter& painter, const Theme& theme) {
    (void)painter;
    (void)theme;
}

} // namespace lumen
