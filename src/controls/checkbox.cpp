#include "fluentui/CheckBox.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"

namespace fui {
namespace {
constexpr float kBoxSize = 18.0f;   // Fluent 复选框 18×18，圆角 4.5
constexpr float kGap = 8.0f;

Color Rgba(uint32_t rgb, float alpha) { return Color::Hex(rgb, alpha); }

} // namespace

void CheckBox::RelayoutParent() { Control::RelayoutParent(); }

void CheckBox::SetChecked(bool value) {
    if (checked_ == value) return;
    checked_ = value;
    Invalidate();
}

Size CheckBox::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kBoxSize : kBoxSize + kGap +
                        UiText().MeasureText(text_, TextRole::Body).w;
    return {width, 28.0f};
}

void CheckBox::Draw(Painter& painter, const Theme& theme) {
    const Rect box{absolute_.x, absolute_.y + (absolute_.h - kBoxSize) * 0.5f, kBoxSize,
                   kBoxSize};
    const float radius = 4.5f;

    Color fill, border, mark;
    if (checked_) {
        // 选中：accent 底 + 同色描边，勾为两段圆头折线（暗色主题黑勾、亮色白勾）
        fill = theme.accent;
        border = fill;
        mark = theme.dark ? Color::Hex(0x000000) : Color::Hex(0xFFFFFF);
        if (hovered_) fill = border = theme.accent_hover;
        if (pressed_) fill = border = theme.accent_pressed;
        if (!enabled_) {
            fill = theme.dark ? Rgba(0xFFFFFF, 41.0f / 255.0f) : Rgba(0x000000, 56.0f / 255.0f);
            border = Color{0, 0, 0, 0};
            mark.a *= 0.8f;
        }
    } else {
        border = theme.dark ? Rgba(0xFFFFFF, 141.0f / 255.0f) : Rgba(0x000000, 122.0f / 255.0f);
        fill = theme.dark ? Rgba(0x000000, 26.0f / 255.0f) : Rgba(0x000000, 6.0f / 255.0f);
        if (enabled_) {
            if (hovered_) {
                fill = theme.dark ? Rgba(0xFFFFFF, 11.0f / 255.0f) : Rgba(0x000000, 13.0f / 255.0f);
            }
            if (pressed_) {
                border = theme.dark ? Rgba(0xFFFFFF, 40.0f / 255.0f) : Rgba(0x000000, 69.0f / 255.0f);
                fill = theme.dark ? Rgba(0xFFFFFF, 18.0f / 255.0f) : Rgba(0x000000, 31.0f / 255.0f);
            }
        } else {
            border = theme.dark ? Rgba(0xFFFFFF, 41.0f / 255.0f) : Rgba(0x000000, 56.0f / 255.0f);
            fill = Color{0, 0, 0, 0};
        }
    }

    painter.FillRoundedRect(box, radius, fill);
    painter.StrokeRoundedRect(box, radius, border);
    if (focused_ && enabled_) painter.DrawFocusRing(box, radius, theme.accent,
                                                    theme.focus_ring_width);
    if (checked_) {
        // 折线勾选：(left+4.2, 中线) → (left+7.6, bottom−4.2) → (right−3.5, top+4.0)
        const float cy = box.y + box.h * 0.5f;
        painter.DrawLine({box.x + 4.2f, cy}, {box.x + 7.6f, box.Bottom() - 4.2f}, mark, 1.7f);
        painter.DrawLine({box.x + 7.6f, box.Bottom() - 4.2f},
                         {box.Right() - 3.5f, box.y + 4.0f}, mark, 1.7f);
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
    if (toggled_) toggled_();
    Invalidate();
}

bool CheckBox::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        checked_ = !checked_;
        if (toggled_) toggled_();
        Invalidate();
        return true;
    }
    return false;
}

} // namespace fui
