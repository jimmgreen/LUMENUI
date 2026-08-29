#include "fluentui/RadioButton.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"

namespace fui {
namespace {
constexpr float kCircleSize = 20.0f;
constexpr float kGap = 8.0f;

Color Rgba(uint32_t rgb, float alpha) { return Color::Hex(rgb, alpha); }

} // namespace

void RadioButton::RelayoutParent() { Control::RelayoutParent(); }

void RadioButton::SetChecked(bool value) {
    if (checked_ == value) return;
    checked_ = value;
    Invalidate();
}

Size RadioButton::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kCircleSize : kCircleSize + kGap +
                        UiText().MeasureText(text_, TextRole::Body).w;
    return {width, 28.0f};
}

void RadioButton::Draw(Painter& painter, const Theme& theme) {
    const Rect circle{absolute_.x, absolute_.y + (absolute_.h - kCircleSize) * 0.5f,
                      kCircleSize, kCircleSize};
    const float outer_radius = kCircleSize * 0.5f;

    if (checked_) {
        // 选中：accent 粗环（5px，悬停且未按下时 4px），内部填主题底色
        float thickness = hovered_ && !pressed_ ? 4.0f : 5.0f;
        Color border = theme.accent;
        Color fill = theme.dark ? Color::Hex(0x000000) : Color::Hex(0xFFFFFF);
        if (!enabled_) {
            thickness = 5.0f;
            border = theme.dark ? Rgba(0xFFFFFF, 40.0f / 255.0f) : Rgba(0x000000, 55.0f / 255.0f);
        }
        painter.FillRoundedRect(circle, outer_radius, fill);
        painter.StrokeRoundedRect(circle, outer_radius, border, thickness);
    } else {
        const float thickness = 1.0f;
        Color border = theme.dark ? Rgba(0xFFFFFF, 153.0f / 255.0f) : Rgba(0x000000, 153.0f / 255.0f);
        Color fill = theme.dark ? Rgba(0x000000, 26.0f / 255.0f) : Rgba(0x000000, 6.0f / 255.0f);
        if (enabled_) {
            if (hovered_) {
                fill = theme.dark ? Rgba(0xFFFFFF, 11.0f / 255.0f) : Rgba(0x000000, 15.0f / 255.0f);
            }
            if (pressed_) {
                border = theme.dark ? Rgba(0xFFFFFF, 40.0f / 255.0f) : Rgba(0x000000, 55.0f / 255.0f);
                fill = theme.dark ? Color::Hex(0x000000) : Color::Hex(0xFFFFFF);
            }
        } else {
            border = theme.dark ? Rgba(0xFFFFFF, 41.0f / 255.0f) : Rgba(0x000000, 55.0f / 255.0f);
            fill = Color{0, 0, 0, 0};
        }
        painter.FillRoundedRect(circle, outer_radius, fill);
        painter.StrokeRoundedRect(circle, outer_radius, border, thickness);
        // 按下时的额外按压圈：r=7、线宽 4
        if (pressed_ && enabled_) {
            const Rect press_ring{circle.x + 3.0f, circle.y + 3.0f, 14.0f, 14.0f};
            painter.StrokeRoundedRect(
                press_ring, 7.0f,
                theme.dark ? Rgba(0xFFFFFF, 40.0f / 255.0f) : Rgba(0x000000, 24.0f / 255.0f),
                4.0f);
        }
    }
    if (focused_ && enabled_) {
        painter.DrawFocusRing(circle, outer_radius, theme.accent, theme.focus_ring_width);
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
                    other->Invalidate();
                }
            }
        }
    }
    checked_ = true;
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
