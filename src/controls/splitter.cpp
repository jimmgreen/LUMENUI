#include "lumen/Splitter.h"
#include "lumen/Painter.h"
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {
constexpr float kPi = 3.14159265f;
}  // namespace

Size Splitter::Measure(Size available, const Theme&) {
    const auto along = [](float v) { return (v > 0.0f && v < 1.0e4f) ? v : 24.0f; };
    if (orientation_ == Orientation::Vertical) {
        return {thickness_, along(available.h)};
    }
    return {along(available.w), thickness_};
}

CursorShape Splitter::CursorAt(Point) const {
    return orientation_ == Orientation::Vertical ? CursorShape::SizeWE : CursorShape::SizeNS;
}

void Splitter::OnMouseEnter() {
    Control::OnMouseEnter();
    Animate();
}

void Splitter::OnMouseLeave() {
    Control::OnMouseLeave();
    if (!dragging_) Animate();
}

float Splitter::WindowAxis(Point local) const noexcept {
    return orientation_ == Orientation::Vertical ? absolute_.x + local.x
                                                 : absolute_.y + local.y;
}

void Splitter::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001) || !enabled_) return;
    dragging_ = true;
    last_axis_ = WindowAxis(local);
    Animate();
    Invalidate();
}

void Splitter::OnMouseMove(Point local, uint32_t) {
    if (!dragging_ || dragged_.Empty()) return;
    const float axis = WindowAxis(local);
    const float delta = axis - last_axis_;
    if (std::fabs(delta) < 0.01f) return;
    last_axis_ = axis;
    dragged_.Emit(delta);
}

void Splitter::OnMouseUp(Point, uint32_t) {
    if (!dragging_) return;
    dragging_ = false;
    ended_.Emit();
    Animate();
    Invalidate();
}

bool Splitter::OnAnimate(float dt_seconds) {
    const float glow_target = (hovered_ || dragging_) ? 1.0f : 0.0f;
    const float drag_target = dragging_ ? 1.0f : 0.0f;
    bool more = EaseTo(glow_t_, glow_target, dt_seconds, 14.0f, 0.004f);
    more = EaseTo(drag_t_, drag_target, dt_seconds, 16.0f, 0.004f) || more;
    if (hovered_ && !dragging_) {
        breathe_phase_ += dt_seconds * kPi * 0.9f;
        if (breathe_phase_ > kPi * 2.0f) breathe_phase_ -= kPi * 2.0f;
        breathe_ = 0.5f + 0.5f * std::sin(breathe_phase_);
        more = true;
    } else {
        more = EaseTo(breathe_, dragging_ ? 1.0f : 0.0f, dt_seconds, 10.0f, 0.01f) || more;
    }
    if (more) Invalidate();
    return more || Control::OnAnimate(dt_seconds);
}

void Splitter::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    const bool vertical = orientation_ == Orientation::Vertical;
    const float mid = vertical ? absolute_.x + absolute_.w * 0.5f
                               : absolute_.y + absolute_.h * 0.5f;
    const float cx = absolute_.x + absolute_.w * 0.5f;
    const float cy = absolute_.y + absolute_.h * 0.5f;
    constexpr float kHandle = 28.0f;
    constexpr float kThick = 3.0f;
    const float rad = kThick * 0.5f;
    const Rect handle = vertical
        ? Rect{mid - kThick * 0.5f, cy - kHandle * 0.5f, kThick, kHandle}
        : Rect{cx - kHandle * 0.5f, mid - kThick * 0.5f, kHandle, kThick};

    // 3px stadium: DrawGlow clamps blur to <1px, dilated FillRoundedRect is a hard pill.
    // Radial bloom mutates radius (brush cache stays put); alpha only tracks glow_t_/drag_t_.
    auto bloom = [&](float radius, Color inner, float stop) {
        if (inner.a <= 0.004f || radius <= 0.5f) return;
        Color outer = inner;
        outer.a = 0.0f;
        painter.FillRectRadial({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f},
                               {cx, cy}, radius, inner, outer, stop);
    };

    if (glow_t_ > 0.01f) {
        const float r_far = Lerp(Lerp(16.0f, 26.0f, breathe_), 40.0f, drag_t_);
        Color halo = theme.glow_md;
        halo.a *= glow_t_;
        bloom(r_far, halo, 0.88f);

        Color hot = theme.glow_lg;
        hot.a *= glow_t_ * Lerp(0.45f, 0.85f, drag_t_);
        bloom(r_far * Lerp(0.40f, 0.48f, drag_t_), hot, 0.80f);
    }

    Color core = theme.stroke_divider;
    core.a = Clamp(0.22f + glow_t_ * 0.50f + drag_t_ * 0.28f, 0.0f, 1.0f);
    painter.FillRoundedRect(handle, rad, core);
}

} // namespace lumen
