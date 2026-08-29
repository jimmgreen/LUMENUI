#include "fluentui/CheckBox.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"

namespace fui {
namespace {
constexpr float kBoxSize = 20.0f;
constexpr float kGap = 10.0f;

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}
} // namespace

void CheckBox::RelayoutParent() { Control::RelayoutParent(); }

void CheckBox::SetChecked(bool value) {
    if (checked_ == value) return;
    checked_ = value;
    Animate();
    Invalidate();
}

Size CheckBox::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kBoxSize : kBoxSize + kGap +
                        UiText().MeasureText(text_, TextRole::Body).w;
    return {width, 28.0f};
}

void CheckBox::Draw(Painter& painter, const Theme& theme) {
    const Rect box{absolute_.x, absolute_.y + (absolute_.h - kBoxSize) * 0.5f, kBoxSize, kBoxSize};
    const float check_t = (checked_ ? 1.0f : 0.0f);

    Color fill = Mix(theme.control_fill, theme.control_fill_hover, hover_t_);
    fill = Mix(fill, theme.control_fill_pressed, press_t_);
    Color border = theme.control_stroke_strong;
    Color mark = theme.text;
    if (check_t > 0.0f) {
        fill = Mix(fill, theme.accent, check_t);
        border = Mix(border, theme.accent, check_t);
        mark = Mix(mark, theme.accent_text, check_t);
    }
    if (!enabled_) {
        fill.a *= 0.5f;
        mark = theme.text_disabled;
        border.a *= 0.5f;
    }
    painter.FillRoundedRect(box, 4.0f, fill);
    painter.StrokeRoundedRect(box, 4.0f, border);
    if (focused_ && enabled_) painter.DrawFocusRing(box, 4.0f, theme.focus_ring);
    if (check_t > 0.05f) {
        Color icon_color = mark;
        icon_color.a *= check_t;
        painter.DrawIcon(L"\uE73E", box, 14.0f, icon_color);
    }

    if (!text_.empty()) {
        painter.DrawText(text_,
                         {absolute_.x + kBoxSize + kGap, absolute_.y,
                          UiText().MeasureText(text_, TextRole::Body).w + 2.0f, absolute_.h},
                         TextRole::Body, enabled_ ? theme.text : theme.text_disabled);
    }
}

void CheckBox::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    if (local.x < -4.0f || local.y < -4.0f || local.x > absolute_.w + 4.0f ||
        local.y > absolute_.h + 4.0f)
        return;
    checked_ = !checked_;
    Animate();
    if (toggled_) toggled_();
    Invalidate();
}

bool CheckBox::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        checked_ = !checked_;
        Animate();
        if (toggled_) toggled_();
        Invalidate();
        return true;
    }
    return false;
}

} // namespace fui
