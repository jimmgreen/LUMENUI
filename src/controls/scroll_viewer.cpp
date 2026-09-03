#include "lumen/ScrollViewer.h"
#include "lumen/Painter.h"
#include "lumen/Theme.h"
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {

constexpr float kInf = 1.0e5f;
constexpr float kWheel = 48.0f;
constexpr float kBarHit = 10.0f;
constexpr float kSlack = 48.0f;
constexpr float kRubber = 0.3f;

bool AxisFinite(float v) noexcept { return v >= 0.0f && v < 1.0e4f; }

float Rubber(float value, float delta, float lo, float hi, float slack) noexcept {
    float next = value + delta;
    if (next < lo) {
        const float in_range = value > lo ? std::min(value - lo, -delta) : 0.0f;
        next = value - in_range - (-delta - in_range) * kRubber;
    } else if (next > hi) {
        const float in_range = value < hi ? std::min(hi - value, delta) : 0.0f;
        next = value + in_range + (delta - in_range) * kRubber;
    }
    return Clamp(next, lo - slack, hi + slack);
}

} // namespace

float ScrollViewer::MaxScrollX() const noexcept {
    return std::max(0.0f, content_w_ - absolute_.w);
}

float ScrollViewer::MaxScrollY() const noexcept {
    return std::max(0.0f, content_h_ - absolute_.h);
}

void ScrollViewer::SnapToTarget() {
    scroll_x_ = target_x_;
    scroll_y_ = target_y_;
    spring_x_.Snap(scroll_x_);
    spring_y_.Snap(scroll_y_);
}

void ScrollViewer::ClampOffsets() {
    const float mx = MaxScrollX();
    const float my = MaxScrollY();
    const float slack = window_ ? kSlack : 0.0f;
    target_x_ = Clamp(target_x_, -slack, mx + slack);
    target_y_ = Clamp(target_y_, -slack, my + slack);
    if (!window_) {
        target_x_ = Clamp(target_x_, 0.0f, mx);
        target_y_ = Clamp(target_y_, 0.0f, my);
        SnapToTarget();
        return;
    }
    scroll_x_ = Clamp(scroll_x_, -slack, mx + slack);
    scroll_y_ = Clamp(scroll_y_, -slack, my + slack);
}

void ScrollViewer::ScrollAxisTo(float& target, float& scroll, SpringMotion& spring, float value,
                                bool animate) {
    target = value;
    ClampOffsets();
    if (!window_ || !animate || MotionScale() <= 0.001f) {
        scroll = target;
        spring.Snap(scroll);
        PlaceContent();
    } else {
        Animate();
    }
    Invalidate();
}

ScrollViewer& ScrollViewer::ScrollToX(float value, bool animate) {
    ScrollAxisTo(target_x_, scroll_x_, spring_x_, value, animate);
    return *this;
}

ScrollViewer& ScrollViewer::ScrollToY(float value, bool animate) {
    ScrollAxisTo(target_y_, scroll_y_, spring_y_, value, animate);
    return *this;
}

ScrollViewer& ScrollViewer::AnchorEnabled(bool on) {
    anchor_enabled_ = on;
    return *this;
}

ScrollViewer& ScrollViewer::AnchorRatio(float y) {
    anchor_ratio_y_ = Clamp(y, 0.0f, 1.0f);
    return *this;
}

ScrollViewer& ScrollViewer::Anchor(Control* child) {
    anchor_ = child;
    return *this;
}

bool ScrollViewer::OwnsDescendant(const Control& node) const {
    const auto walk = [&](auto&& self, const Panel& panel) -> bool {
        for (size_t i = 0; i < panel.ChildCount(); ++i) {
            const Control& child = panel.Child(i);
            if (&child == &node) return true;
            if (const Panel* nested = child.AsPanel()) {
                if (self(self, *nested)) return true;
            }
        }
        return false;
    };
    return walk(walk, *this);
}

Control* ScrollViewer::DeepestAt(Control& node, Point world) const {
    if (!node.Visible() || !node.AbsoluteBounds().Contains(world)) return nullptr;
    if (Panel* panel = node.AsPanel()) {
        for (size_t i = panel->ChildCount(); i > 0; --i) {
            if (Control* hit = DeepestAt(panel->Child(i - 1), world)) return hit;
        }
    }
    return &node;
}

Control* ScrollViewer::ResolveAnchor() {
    if (anchor_ && anchor_->Visible() && OwnsDescendant(*anchor_)) return anchor_;
    if (absolute_.IsEmpty()) return nullptr;
    const float inset = 2.0f;
    const float span = std::max(0.0f, absolute_.h - inset * 2.0f);
    const float y = absolute_.y + inset + Clamp(anchor_ratio_y_, 0.0f, 1.0f) * span;
    Control* hit = DeepestAt(*this, {absolute_.x + std::min(4.0f, std::max(0.0f, absolute_.w * 0.5f)), y});
    return hit == this ? nullptr : hit;
}

void ScrollViewer::ApplyAnchor(Control* keep, float rel_x, float rel_y) {
    if (!keep || !OwnsDescendant(*keep)) return;
    const float nx = keep->AbsoluteBounds().x - absolute_.x;
    const float ny = keep->AbsoluteBounds().y - absolute_.y;
    bool moved = false;
    if (horizontal_ && std::fabs(nx - rel_x) > 0.5f) {
        target_x_ += nx - rel_x;
        moved = true;
    }
    if (vertical_ && std::fabs(ny - rel_y) > 0.5f) {
        target_y_ += ny - rel_y;
        moved = true;
    }
    if (!moved) return;
    target_x_ = Clamp(target_x_, 0.0f, MaxScrollX());
    target_y_ = Clamp(target_y_, 0.0f, MaxScrollY());
    SnapToTarget();
    PlaceContent();
}

void ScrollViewer::BringIntoView(const Rect& absolute_child, ScrollAlignment align) {
    if (absolute_.IsEmpty() || absolute_child.IsEmpty()) return;
    float dx = 0.0f;
    float dy = 0.0f;
    if (horizontal_) {
        switch (align) {
        case ScrollAlignment::Start:
            dx = absolute_child.x - absolute_.x;
            break;
        case ScrollAlignment::End:
            dx = absolute_child.Right() - absolute_.Right();
            break;
        case ScrollAlignment::Center:
            dx = (absolute_child.x + absolute_child.w * 0.5f) -
                 (absolute_.x + absolute_.w * 0.5f);
            break;
        case ScrollAlignment::Nearest:
        default:
            if (absolute_child.x < absolute_.x) dx = absolute_child.x - absolute_.x;
            else if (absolute_child.Right() > absolute_.Right()) {
                dx = absolute_child.Right() - absolute_.Right();
            }
            break;
        }
    }
    if (vertical_) {
        switch (align) {
        case ScrollAlignment::Start:
            dy = absolute_child.y - absolute_.y;
            break;
        case ScrollAlignment::End:
            dy = absolute_child.Bottom() - absolute_.Bottom();
            break;
        case ScrollAlignment::Center:
            dy = (absolute_child.y + absolute_child.h * 0.5f) -
                 (absolute_.y + absolute_.h * 0.5f);
            break;
        case ScrollAlignment::Nearest:
        default:
            if (absolute_child.y < absolute_.y) dy = absolute_child.y - absolute_.y;
            else if (absolute_child.Bottom() > absolute_.Bottom()) {
                dy = absolute_child.Bottom() - absolute_.Bottom();
            }
            break;
        }
    }
    if (std::fabs(dx) < 0.5f && std::fabs(dy) < 0.5f) return;
    if (std::fabs(dx) >= 0.5f) ScrollToX(scroll_x_ + dx);
    if (std::fabs(dy) >= 0.5f) ScrollToY(scroll_y_ + dy);
}

void ScrollViewer::EnsureVisible(const Rect& absolute_child) {
    BringIntoView(absolute_child, ScrollAlignment::Nearest);
}

void ScrollViewer::ScrollIntoView(Control& child, ScrollAlignment align) {
    if (!child.Visible() || !OwnsDescendant(child)) return;
    BringIntoView(child.AbsoluteBounds(), align);
}

Rect ScrollViewer::VerticalTrack() const noexcept {
    return {absolute_.Right() - kBarHit, absolute_.y, kBarHit, absolute_.h};
}

Rect ScrollViewer::HorizontalTrack() const noexcept {
    return {absolute_.x, absolute_.Bottom() - kBarHit, absolute_.w, kBarHit};
}

Color ScrollViewer::ThumbColor(const Theme& theme) const noexcept {
    const float lit = std::max(expand_t_, drag_axis_ != 0 ? 1.0f : 0.0f);
    return {Lerp(theme.scrollbar_thumb.r, theme.scrollbar_thumb_hover.r, lit),
            Lerp(theme.scrollbar_thumb.g, theme.scrollbar_thumb_hover.g, lit),
            Lerp(theme.scrollbar_thumb.b, theme.scrollbar_thumb_hover.b, lit),
            Lerp(theme.scrollbar_thumb.a, theme.scrollbar_thumb_hover.a, lit)};
}

void ScrollViewer::PlaceContent() {
    const float pad = std::max(absolute_.h, 1.0f);
    float y = -scroll_y_;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size& d = ChildDesired(i);
        const float w = horizontal_ ? std::max(d.w, content_w_) : std::max(d.w, absolute_.w);
        SetChildBounds(Child(i), {-scroll_x_, y, w, d.h});
        const float child_top = y;
        const float child_bottom = y + d.h;
        if (child_bottom >= -pad && child_top <= absolute_.h + pad) {
            ArrangeChildAt(i);
        } else {
            SyncChildAbsolute(i);
        }
        y += d.h;
    }
}

Size ScrollViewer::Measure(Size available, const Theme& theme) {
    const bool width_finite = AxisFinite(available.w);
    const Size child_av{(!horizontal_ && width_finite) ? available.w : kInf, kInf};
    content_w_ = 0.0f;
    content_h_ = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, child_av, theme);
        content_w_ = std::max(content_w_, desired.w);
        content_h_ += desired.h;
    }
    const float w = width_finite ? available.w : content_w_;
    const float h = AxisFinite(available.h) ? available.h : content_h_;
    return {w, h};
}

void ScrollViewer::Arrange(const Rect& absolute) {
    Control* keep = nullptr;
    float rel_x = 0.0f;
    float rel_y = 0.0f;
    const bool capture = anchor_enabled_ && !absolute_.IsEmpty() && drag_axis_ == 0 &&
                         !panning_content_;
    if (capture) {
        keep = ResolveAnchor();
        if (keep) {
            rel_x = keep->AbsoluteBounds().x - absolute_.x;
            rel_y = keep->AbsoluteBounds().y - absolute_.y;
        }
    }
    absolute_ = absolute;
    ClampOffsets();
    PlaceContent();
    if (keep) ApplyAnchor(keep, rel_x, rel_y);
}

bool ScrollViewer::CapturesOverlay(Point p) const {
    if (drag_axis_ != 0 || panning_content_) return true;
    if (vertical_ && MaxScrollY() > 0.5f && VerticalTrack().Contains(p)) return true;
    if (horizontal_ && MaxScrollX() > 0.5f && HorizontalTrack().Contains(p)) return true;
    return false;
}

bool ScrollViewer::CanPan() const noexcept {
    return (vertical_ && MaxScrollY() > 0.5f) || (horizontal_ && MaxScrollX() > 0.5f);
}

void ScrollViewer::PanBy(float dx, float dy) {
    panning_content_ = true;
    const float slack = window_ ? kSlack : 0.0f;
    if (vertical_) {
        scroll_y_ = Rubber(scroll_y_, -dy, 0.0f, MaxScrollY(), slack);
        target_y_ = scroll_y_;
        spring_y_.Snap(scroll_y_);
    }
    if (horizontal_) {
        scroll_x_ = Rubber(scroll_x_, -dx, 0.0f, MaxScrollX(), slack);
        target_x_ = scroll_x_;
        spring_x_.Snap(scroll_x_);
    }
    PlaceContent();
    wheel_t_ = 1.0f;
    hide_idle_ = 0.0f;
    Invalidate();
}

void ScrollViewer::PanFling(float vx, float vy) {
    panning_content_ = false;
    if (vertical_) {
        spring_y_.velocity = -vy;
        target_y_ = scroll_y_ - vy * 0.22f;
    }
    if (horizontal_) {
        spring_x_.velocity = -vx;
        target_x_ = scroll_x_ - vx * 0.22f;
    }
    ClampOffsets();
    Animate();
}

bool ScrollViewer::OnWheel(float delta) {
    if (!vertical_ || MaxScrollY() <= 0.0f) return false;
    const float slack = window_ ? kSlack : 0.0f;
    target_y_ = Rubber(target_y_, -delta * kWheel, 0.0f, MaxScrollY(), slack);
    wheel_t_ = 1.0f;
    hide_idle_ = 0.0f;
    if (std::fabs(delta) < 0.35f || !window_) {
        target_y_ = Clamp(target_y_, 0.0f, MaxScrollY());
        scroll_y_ = target_y_;
        spring_y_.Snap(scroll_y_);
        PlaceContent();
        Invalidate();
        return true;
    }
    Animate();
    return true;
}

bool ScrollViewer::OnHWheel(float delta) {
    if (!horizontal_ || MaxScrollX() <= 0.0f) return false;
    const float slack = window_ ? kSlack : 0.0f;
    target_x_ = Rubber(target_x_, delta * kWheel, 0.0f, MaxScrollX(), slack);
    wheel_t_ = 1.0f;
    hide_idle_ = 0.0f;
    if (std::fabs(delta) < 0.35f || !window_) {
        target_x_ = Clamp(target_x_, 0.0f, MaxScrollX());
        scroll_x_ = target_x_;
        spring_x_.Snap(scroll_x_);
        PlaceContent();
        Invalidate();
        return true;
    }
    Animate();
    return true;
}

void ScrollViewer::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;   // MK_LBUTTON
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (vertical_ && MaxScrollY() > 0.5f && VerticalTrack().Contains(world)) {
        const ScrollThumb thumb =
            MakeScrollThumb(absolute_, content_h_, scroll_y_, 1.0f, true);
        drag_axis_ = 1;
        if (thumb.visible && thumb.rect.Contains(world)) {
            drag_grab_ = world.y - thumb.rect.y;
        } else {
            const float track = std::max(1.0f, absolute_.h - (thumb.visible ? thumb.rect.h : 20.0f));
            const float t = Clamp((local.y - (thumb.visible ? thumb.rect.h * 0.5f : 10.0f)) / track,
                                  0.0f, 1.0f);
            ScrollToY(t * MaxScrollY());
            drag_grab_ = thumb.visible ? thumb.rect.h * 0.5f : 10.0f;
        }
        Animate();
        return;
    }
    if (horizontal_ && MaxScrollX() > 0.5f && HorizontalTrack().Contains(world)) {
        const ScrollThumb thumb =
            MakeScrollThumb(absolute_, content_w_, scroll_x_, 1.0f, false);
        drag_axis_ = 2;
        if (thumb.visible && thumb.rect.Contains(world)) {
            drag_grab_ = world.x - thumb.rect.x;
        } else {
            const float track = std::max(1.0f, absolute_.w - (thumb.visible ? thumb.rect.w : 20.0f));
            const float t = Clamp((local.x - (thumb.visible ? thumb.rect.w * 0.5f : 10.0f)) / track,
                                  0.0f, 1.0f);
            ScrollToX(t * MaxScrollX());
            drag_grab_ = thumb.visible ? thumb.rect.w * 0.5f : 10.0f;
        }
        Animate();
    }
}

void ScrollViewer::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    if (drag_axis_ == 1) {
        const ScrollThumb thumb =
            MakeScrollThumb(absolute_, content_h_, scroll_y_, 1.0f, true);
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, absolute_.h - thumb_h);
        const float t = Clamp((local.y - drag_grab_) / track, 0.0f, 1.0f);
        scroll_y_ = target_y_ = t * MaxScrollY();
        spring_y_.Snap(scroll_y_);
        PlaceContent();
        Invalidate();
        return;
    }
    if (drag_axis_ == 2) {
        const ScrollThumb thumb =
            MakeScrollThumb(absolute_, content_w_, scroll_x_, 1.0f, false);
        const float thumb_w = thumb.visible ? thumb.rect.w : 20.0f;
        const float track = std::max(1.0f, absolute_.w - thumb_w);
        const float t = Clamp((local.x - drag_grab_) / track, 0.0f, 1.0f);
        scroll_x_ = target_x_ = t * MaxScrollX();
        spring_x_.Snap(scroll_x_);
        PlaceContent();
        Invalidate();
    }
}

void ScrollViewer::OnMouseUp(Point, uint32_t) {
    drag_axis_ = 0;
    Animate();
}

void ScrollViewer::OnMouseLeave() {
    Control::OnMouseLeave();
    if (drag_axis_ == 0) Animate();
}

bool ScrollViewer::OnAnimate(float dt) {
    bool more = Control::OnAnimate(dt);
    const float mx = MaxScrollX();
    const float my = MaxScrollY();
    if (drag_axis_ == 0 && !panning_content_ && wheel_t_ < 0.08f) {
        if (target_x_ < 0.0f) target_x_ = 0.0f;
        else if (target_x_ > mx) target_x_ = mx;
        if (target_y_ < 0.0f) target_y_ = 0.0f;
        else if (target_y_ > my) target_y_ = my;
    }
    if (MotionScale() <= 0.001f) {
        SnapToTarget();
    } else {
        spring_x_.value = scroll_x_;
        spring_y_.value = scroll_y_;
        const bool over_x = scroll_x_ < -0.5f || scroll_x_ > mx + 0.5f;
        const bool over_y = scroll_y_ < -0.5f || scroll_y_ > my + 0.5f;
        more |= spring_x_.Tick(target_x_, dt, over_x ? Spring::Snappy() : Spring::Smooth());
        more |= spring_y_.Tick(target_y_, dt, over_y ? Spring::Snappy() : Spring::Smooth());
        scroll_x_ = spring_x_.value;
        scroll_y_ = spring_y_.value;
    }
    const bool bar_hot = drag_axis_ != 0 || hovered_ || wheel_t_ > 0.01f;
    if (bar_hot) hide_idle_ = 0.0f;
    else hide_idle_ += dt;
    const float alpha_target = (bar_hot || hide_idle_ < 1.0f) ? 1.0f : 0.0f;
    more |= EaseTo(expand_t_, bar_hot ? 1.0f : 0.0f, dt, 18.0f);
    more |= EaseTo(wheel_t_, 0.0f, dt, 4.0f);
    more |= EaseTo(thumb_alpha_, alpha_target, dt, 10.0f);
    if (std::fabs(scroll_x_ - target_x_) > 0.05f || std::fabs(scroll_y_ - target_y_) > 0.05f) {
        PlaceContent();
    }
    return more;
}

void ScrollViewer::DrawOverlay(Painter& painter, const Theme& theme) {
    ClampOffsets();
    Color color = ThumbColor(theme);
    color.a *= thumb_alpha_;
    if (color.a <= 0.004f) return;
    if (vertical_) {
        painter.DrawScrollThumb(MakeScrollThumb(absolute_, content_h_, scroll_y_, expand_t_, true),
                                color);
    }
    if (horizontal_) {
        painter.DrawScrollThumb(
            MakeScrollThumb(absolute_, content_w_, scroll_x_, expand_t_, false), color);
    }
}

} // namespace lumen
