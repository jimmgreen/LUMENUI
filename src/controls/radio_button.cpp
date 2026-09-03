#include "lumen/RadioButton.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"

namespace lumen {
namespace {
constexpr float kCircleSize = 20.0f;
constexpr float kGap = 12.0f;

} // namespace

void RadioButton::RelayoutParent() { Control::RelayoutParent(); }

RadioButton& RadioButton::Checked(bool value) {
    if (checked_ == value) return *this;
    checked_ = value;
    Invalidate();
    return *this;
}

Size RadioButton::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kCircleSize : kCircleSize + kGap +
                        MeasureText(text_, TextRole::Body).w;
    return {width, 28.0f};
}

void RadioButton::Draw(Painter& painter, const Theme& theme) {
    const Rect circle{absolute_.x, absolute_.y + (absolute_.h - kCircleSize) * 0.5f,
                      kCircleSize, kCircleSize};
    const float outer_radius = kCircleSize * 0.5f;

    if (checked_) {
        const Color fill = enabled_ ? theme.bg : theme.fill_input_disabled;
        const Color border = enabled_ ? theme.accent
                                       : Color{theme.accent.r, theme.accent.g, theme.accent.b,
                                               0.16f * theme.glow_intensity};
        if (enabled_) painter.DrawGlow(circle, outer_radius, theme.glow_sm);
        painter.FillRoundedRect(circle, outer_radius, fill);
        painter.StrokeRoundedRect(circle, outer_radius, border, 1.0f);
        const float dot = 10.0f;
        const Rect inner{circle.x + (kCircleSize - dot) * 0.5f,
                         circle.y + (kCircleSize - dot) * 0.5f, dot, dot};
        painter.FillRoundedRect(inner, dot * 0.5f, enabled_ ? theme.accent : theme.text_disabled);
    } else {
        const Color border =
            Color{theme.accent.r, theme.accent.g, theme.accent.b,
                  (enabled_ ? (hovered_ ? 0.60f : 0.30f) : 0.16f) * theme.glow_intensity};
        painter.FillRoundedRect(circle, outer_radius, theme.bg);
        painter.StrokeRoundedRect(circle, outer_radius, border, 1.0f);
        if (enabled_ && hovered_) {
            Color hover_glow = theme.glow_sm;
            hover_glow.a *= 0.40f;
            painter.DrawGlow(circle, outer_radius, hover_glow);
        }
    }
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, circle, outer_radius);
    }

    if (!text_.empty()) {
        const Color label = enabled_ ? (checked_ ? theme.text : theme.text_secondary)
                                     : theme.text_disabled;
        painter.DrawText(text_,
                         {absolute_.x + kCircleSize + kGap, absolute_.y,
                          MeasureText(text_, TextRole::Body).w, absolute_.h},
                         TextRole::Body, label);
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
    toggled_.Emit(true);
    Invalidate();
}

bool RadioButton::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        if (!checked_) {
            SelectExclusive();
            toggled_.Emit(true);
            Invalidate();
        }
        return true;
    }
    return false;
}

} // namespace lumen
