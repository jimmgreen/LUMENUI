#include "lumen/Expander.h"
#include "lumen/Painter.h"
#include "lumen/Animate.h"
#include "../core/text_service.h"
#include "../core/window_impl.h"

namespace lumen {
namespace {
constexpr float kHeaderHeight = 48.0f;
constexpr float kContentPad = 14.0f;
} // namespace

void Expander::RelayoutParent() { Control::RelayoutParent(); }

Expander& Expander::Expanded(bool value) {
    if (expanded_ == value) return *this;
    expanded_ = value;
    // 尚未完成首次布局时直接到位，避免 gallery 初始收起先闪一帧展开。
    if (window_ && absolute_.h > 0.5f) {
        float dur = WindowImpl::ThemeOf(window_).duration_normal * MotionScale();
        const Ease ease = WindowImpl::ThemeOf(window_).ease_standard;
        if (dur <= 0.001f) {
            open_t_ = expanded_ ? 1.0f : 0.0f;
            open_tween_.Snap(open_t_);
        } else {
            open_tween_.Play(open_t_, expanded_ ? 1.0f : 0.0f, dur, ease);
            Animate();
        }
    } else {
        open_t_ = expanded_ ? 1.0f : 0.0f;
        chevron_t_ = open_t_;
        open_tween_.Snap(open_t_);
    }
    RelayoutParent();
    Invalidate();
    changed_.Emit();
    return *this;
}

bool Expander::OnAnimate(float dt) {
    bool active = Control::OnAnimate(dt);
    active |= EaseTo(chevron_t_, expanded_ ? 1.0f : 0.0f, dt, 14.0f);
    if (open_tween_.running) {
        open_tween_.Tick(dt);
        open_t_ = open_tween_.Value();
        RelayoutParent();
        active = true;
    }
    return active;
}

Size Expander::Measure(Size available, const Theme& theme) {
    float content_h = 0.0f;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {available.w - 28.0f, 1.0e5f}, theme);
        content_h += desired.h + (first ? 0.0f : 8.0f);
        first = false;
    }
    const float extra = children_.empty() ? 0.0f : content_h + kContentPad;
    return {std::max(available.w, 240.0f), kHeaderHeight + extra * open_t_};
}

void Expander::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    float y = absolute.y + kHeaderHeight;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = ChildDesired(i);
        SetChildBounds(Child(i), {14.0f, y - absolute.y, absolute.w - 28.0f, desired.h});
        MeasureChildAt(i, {absolute.w - 28.0f, 1.0e5f}, Theme{});
        ArrangeChildAt(i);
        y += desired.h + (first ? 0.0f : 8.0f);
        first = false;
    }
}

void Expander::Draw(Painter& painter, const Theme& theme) {
    painter.FillRoundedRect(absolute_, 12.0f, theme.fill_input);
    if (spotlight_t_ > 0.004f) {
        DrawSpotlight(painter, theme, absolute_, 12.0f, SpotlightCenter(), spotlight_t_);
    } else {
        const Color stroke = hovered_ ? theme.control_stroke : theme.stroke_card;
        painter.StrokeRoundedRect(absolute_, 12.0f, stroke);
    }

    const Color title = expanded_ ? theme.text : (hovered_ ? theme.text : theme.text_secondary);
    painter.DrawText(title_, {absolute_.x + 16.0f, absolute_.y, absolute_.w - 56.0f,
                              kHeaderHeight},
                     TextRole::BodyStrong, title);
    const Point pivot{absolute_.Right() - 28.0f, absolute_.y + kHeaderHeight * 0.5f};
    painter.DrawChevron(pivot, 16.0f, 180.0f * chevron_t_, title, 1.6f);
    if (open_t_ > 0.12f && !children_.empty()) {
        Color rule = theme.stroke_divider;
        rule.a *= open_t_;
        painter.FillRect({absolute_.x + 16.0f, absolute_.y + kHeaderHeight,
                          absolute_.w - 32.0f, 1.0f}, rule);
    }
    if (HasFocus()) {
        PaintFocusRing(painter, theme, absolute_, 12.0f);
    }
}

void Expander::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    if (local.y <= kHeaderHeight) Expanded(!expanded_);
}

bool Expander::OnKey(uint32_t vk) {
    if (vk == VK_SPACE || vk == VK_RETURN) {
        Expanded(!expanded_);
        return true;
    }
    return false;
}

void Expander::OnFocusChanged(bool focused) {
    (void)focused;
    Invalidate();
}

} // namespace lumen
