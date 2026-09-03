#include "lumen/CheckBox.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"

namespace lumen {
namespace {
constexpr float kBoxSize = 20.0f;
constexpr float kGap = 12.0f;
constexpr float kRadius = 4.0f;
constexpr float kCheckSize = 16.0f;
constexpr float kCheckStroke = 2.2f;
constexpr float kGlowSpread = 0.60f;
} // namespace

void CheckBox::RelayoutParent() { Control::RelayoutParent(); }

CheckBox& CheckBox::Checked(bool value) {
    return State(value ? CheckState::Checked : CheckState::Unchecked);
}

CheckBox& CheckBox::State(CheckState value) {
    if (state_ == value) return *this;
    state_ = value;
    Invalidate();
    return *this;
}

void CheckBox::Cycle() {
    if (three_state_) {
        if (state_ == CheckState::Unchecked) state_ = CheckState::Checked;
        else if (state_ == CheckState::Checked) state_ = CheckState::Indeterminate;
        else state_ = CheckState::Unchecked;
    } else {
        state_ = (state_ == CheckState::Checked) ? CheckState::Unchecked : CheckState::Checked;
    }
    toggled_.Emit(Checked());
    Invalidate();
}

Size CheckBox::Measure(Size, const Theme&) {
    const float width = text_.empty() ? kBoxSize
                                      : kBoxSize + kGap +
                                            MeasureText(text_, TextRole::BodyStrong).w;
    return {width, 28.0f};
}

void CheckBox::Draw(Painter& painter, const Theme& theme) {
    const Rect box{absolute_.x, absolute_.y + (absolute_.h - kBoxSize) * 0.5f, kBoxSize,
                   kBoxSize};
    const bool on = state_ != CheckState::Unchecked;

    if (on) {
        const Color fill = enabled_ ? theme.accent
                                    : Color{theme.accent.r, theme.accent.g, theme.accent.b, 0.16f};
        if (enabled_) painter.DrawGlow(box, kRadius, theme.glow_md, kGlowSpread);
        painter.FillRoundedRect(box, kRadius, fill);
        painter.StrokeRoundedRect(box, kRadius, enabled_ ? theme.accent : fill);
        const Color mark = enabled_ ? theme.accent_text : Color{0.0f, 0.0f, 0.0f, 0.45f};
        if (state_ == CheckState::Indeterminate) {
            const float dash_w = box.w * 0.46f;
            const float dash_h = 2.2f;
            painter.FillRoundedRect(
                {box.x + (box.w - dash_w) * 0.5f, box.y + (box.h - dash_h) * 0.5f, dash_w, dash_h},
                1.0f, mark);
        } else {
            painter.DrawCheck({box.x + box.w * 0.5f, box.y + box.h * 0.5f}, kCheckSize, mark,
                              kCheckStroke);
        }
    } else {
        const Color border =
            Color{theme.accent.r, theme.accent.g, theme.accent.b,
                  (enabled_ ? (hovered_ ? 0.60f : 0.30f) : 0.16f) * theme.glow_intensity};
        painter.FillRoundedRect(box, kRadius, theme.bg);
        painter.StrokeRoundedRect(box, kRadius, border);
        if (enabled_ && hovered_) {
            Color hover_glow = theme.glow_sm;
            hover_glow.a *= 0.40f;
            painter.DrawGlow(box, kRadius, hover_glow, 0.40f);
        }
    }
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, box, kRadius);
    }

    if (!text_.empty()) {
        const Color label = enabled_ ? (on ? theme.text : theme.text_secondary)
                                     : theme.text_disabled;
        painter.DrawText(text_,
                         {absolute_.x + kBoxSize + kGap, absolute_.y,
                          MeasureText(text_, TextRole::BodyStrong).w, absolute_.h},
                         TextRole::BodyStrong, label);
    }
}

void CheckBox::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    if (local.x < -4.0f || local.y < -4.0f || local.x > absolute_.w + 4.0f ||
        local.y > absolute_.h + 4.0f)
        return;
    Cycle();
}

bool CheckBox::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE) {
        Cycle();
        return true;
    }
    return false;
}

CheckBox& CheckBox::BindChecked(Property<bool>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Checked(p.Get());
        bind_loop_ = false;
    };
    apply();
    checked_bind_ = ScopedConnection(p.OnChanged([apply](const bool&) { apply(); }));
    checked_ctrl_ = ScopedConnection(toggled_.Connect([this, &p](bool v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = v;
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
