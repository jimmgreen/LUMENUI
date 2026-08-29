#include "fluentui/Control.h"
#include "fluentui/Panel.h"
#include "fluentui/Window.h"
#include "window_impl.h"
#include <cmath>

namespace fui {

Control::~Control() = default;

void Control::SetVisible(bool value) {
    if (visible_ == value) return;
    visible_ = value;
    RelayoutParent();
}

void Control::SetEnabled(bool value) {
    if (enabled_ == value) return;
    enabled_ = value;
    if (!value) {
        pressed_ = false;
        hovered_ = false;
    }
    Invalidate();
}

void Control::SetBounds(const Rect& r) {
    if (bounds_.x == r.x && bounds_.y == r.y && bounds_.w == r.w && bounds_.h == r.h) return;
    bounds_ = r;
    RelayoutParent();
}

Size Control::Measure(Size, const Theme&) {
    return {bounds_.w, bounds_.h};   // 默认：手动定位的控件使用设定尺寸
}

void Control::Arrange(const Rect& absolute) {
    absolute_ = absolute;
}

void Control::OnMouseEnter() {
    hovered_ = true;
    Animate();
}

void Control::OnMouseLeave() {
    hovered_ = false;
    pressed_ = false;
    Animate();
}

bool Control::OnAnimate(float dt_seconds) {
    const float target_hover = hovered_ && enabled_ ? 1.0f : 0.0f;
    const float target_press = pressed_ && enabled_ ? 1.0f : 0.0f;
    bool moving = EaseTo(hover_t_, target_hover, dt_seconds);
    moving |= EaseTo(press_t_, target_press, dt_seconds);
    return moving;
}

bool Control::EaseTo(float& value, float target, float dt, float speed, float epsilon) {
    const float diff = target - value;
    if (std::fabs(diff) <= epsilon) {
        value = target;
        return false;
    }
    value += diff * (1.0f - std::exp(-speed * dt));
    return true;
}

void Control::Invalidate() {
    if (window_) WindowImpl::Invalidate(window_);
}

void Control::Animate() {
    if (window_) WindowImpl::Animate(window_);
}

void Control::RelayoutParent() {
    if (window_) WindowImpl::Relayout(window_);
}

void Control::SetFocus() {
    if (window_) WindowImpl::SetFocusTo(window_, this);
}

void* Control::NativeWindow() const {
    return window_ ? window_->NativeHandle() : nullptr;
}

// ---- Panel ----

Panel::~Panel() = default;

void Panel::Remove(Control& child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == &child) {
            children_.erase(it);
            RelayoutParent();
            return;
        }
    }
}

void Panel::Clear() {
    if (children_.empty()) return;
    children_.clear();
    RelayoutParent();
}

void Panel::Background(Color color) {
    background_ = color;
    Invalidate();
}

void Panel::SetChildBounds(Control& child, const Rect& r) {
    child.bounds_ = r;
}

bool Panel::ChildVisible(size_t index) const {
    return index < children_.size() && children_[index]->visible_;
}

const Size& Panel::ChildDesired(size_t index) const {
    return children_[index]->desired_;
}

void Panel::SetChildVisibility(size_t index, bool visible) {
    if (index < children_.size()) children_[index]->visible_ = visible;
}

Size Panel::MeasureChildAt(size_t index, Size available, const Theme& theme) {
    if (index >= children_.size()) return {};
    Control& child = *children_[index];
    child.desired_ = child.Measure(available, theme);
    return child.desired_;
}

void Panel::ArrangeChildAt(size_t index) {
    if (index >= children_.size()) return;
    Control& child = *children_[index];
    const Rect& b = child.bounds_;
    child.Arrange({absolute_.x + b.x, absolute_.y + b.y, b.w, b.h});
}

Size Panel::Measure(Size, const Theme&) {
    float max_right = 0.0f, max_bottom = 0.0f;
    for (auto& child : children_) {
        if (!child->visible_) continue;
        const Rect& b = child->bounds_;
        max_right = std::max(max_right, b.x + b.w);
        max_bottom = std::max(max_bottom, b.y + b.h);
    }
    return {max_right, max_bottom};
}

void Panel::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        ArrangeChildAt(i);
    }
}

void Panel::Draw(Painter& painter, const Theme&) {
    if (background_.a > 0.0f) painter.FillRect(absolute_, background_);
}

void Panel::Relayout() {
    RelayoutParent();
}

// ---- StackPanel ----

Size StackPanel::Measure(Size available, const Theme& theme) {
    const bool vertical = orientation_ == Orientation::Vertical;
    const float available_cross = (vertical ? available.w : available.h) - padding_h_ * 2.0f;
    float along = vertical ? padding_v_ * 2.0f : padding_h_ * 2.0f;
    float cross = 0.0f;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {available_cross, 1.0e5f}, theme);
        const float extent = vertical ? desired.h : desired.w;
        along += extent + (first ? 0.0f : spacing_);
        cross = std::max(cross, vertical ? desired.w : desired.h);
        first = false;
    }
    return vertical ? Size{cross + padding_h_ * 2.0f, along}
                    : Size{along, cross + padding_v_ * 2.0f};
}

void StackPanel::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const bool vertical = orientation_ == Orientation::Vertical;
    const float cross_size = vertical ? absolute.w : absolute.h;
    float position = vertical ? padding_v_ : padding_h_;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = ChildDesired(i);
        Rect slot;
        if (vertical) {
            slot = {padding_h_, position,
                    std::max(cross_size - padding_h_ * 2.0f, desired.w), desired.h};
        } else {
            slot = {position, padding_v_, desired.w,
                    std::max(cross_size - padding_v_ * 2.0f, desired.h)};
        }
        position += (vertical ? slot.h : slot.w) + (first ? 0.0f : spacing_);
        first = false;
        SetChildBounds(Child(i), slot);
        ArrangeChildAt(i);
    }
}

} // namespace fui
