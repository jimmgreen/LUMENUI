#include "fluentui/RadioButton.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"

namespace fui {
namespace {
constexpr float kCircleSize = 20.0f;
constexpr float kGap = 10.0f;

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}
} // namespace

void RadioButton::RelayoutParent() { Control::RelayoutParent(); }

void RadioButton::SetChecked(bool value) {
    if (checked_ == value) return;
    checked_ = value;
    Animate();
    Invalidate();
}

Size RadioButton::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kCircleSize : kCircleSize + kGap +
                        UiText().MeasureText(text_, TextRole::Body).w;
    return {width, 28.0f};
}

void RadioButton::Draw(Painter& painter, const Theme& theme) {
    const Rect circle{absolute_.x, absolute_.y + (absolute_.h - kCircleSize) * 0.5f, kCircleSize,
                      kCircleSize};
    const float check_t = checked_ ? 1.0f : 0.0f;

    Color fill = Mix(theme.control_fill, theme.control_fill_hover, hover_t_);
    fill = Mix(fill, theme.control_fill_pressed, press_t_);
    Color border = theme.control_stroke_strong;
    if (check_t > 0.0f) {
        fill = Mix(fill, theme.control_fill, check_t);
        border = Mix(border, theme.accent, check_t);
    }
    if (!enabled_) {
        fill.a *= 0.5f;
        border.a *= 0.5f;
    }
    painter.FillRoundedRect(circle, kCircleSize * 0.5f, fill);
    painter.StrokeRoundedRect(circle, kCircleSize * 0.5f, border);
    if (focused_ && enabled_) painter.DrawFocusRing(circle, kCircleSize * 0.5f, theme.focus_ring);
    if (check_t > 0.05f) {
        const Rect dot = circle.Inset(kCircleSize * 0.3f, kCircleSize * 0.3f);
        Color dot_color = theme.accent;
        if (!enabled_) dot_color = theme.text_disabled;
        dot_color.a *= check_t;
        painter.FillRoundedRect(dot, dot.w * 0.5f, dot_color);
    }

    if (!text_.empty()) {
        painter.DrawText(text_,
                         {absolute_.x + kCircleSize + kGap, absolute_.y,
                          UiText().MeasureText(text_, TextRole::Body).w + 2.0f, absolute_.h},
                         TextRole::Body, enabled_ ? theme.text : theme.text_disabled);
    }
}

void RadioButton::SelectExclusive() {
    if (parent_) {
        for (size_t i = 0; i < parent_->ChildCount(); ++i) {
            if (auto* other = dynamic_cast<RadioButton*>(&parent_->Child(i))) {
                if (other != this && other->group_ == group_ && other->checked_) {
                    other->checked_ = false;
                    other->Animate();
                    other->Invalidate();
                }
            }
        }
    }
    checked_ = true;
    Animate();
}

void RadioButton::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    if (local.x < -4.0f || local.y < -4.0f || local.x > absolute_.w + 4.0f ||
        local.y > absolute_.h + 4.0f)
        return;
    if (checked_) return;
    SelectExclusive();
    if (toggled_) toggled_();
    Invalidate();
}

bool RadioButton::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        if (!checked_) {
            SelectExclusive();
            if (toggled_) toggled_();
            Invalidate();
        }
        return true;
    }
    return false;
}

} // namespace fui
