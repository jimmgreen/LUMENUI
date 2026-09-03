#include "lumen/Chip.h"
#include "lumen/Icons.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>

namespace lumen {
namespace {
constexpr float kCloseW = 22.0f;
constexpr uint32_t kMkLeft = 0x0001;
} // namespace

void Chip::RelayoutParent() { Control::RelayoutParent(); }

Size Chip::Measure(Size, const Theme&) {
    float width = MeasureText(text_, TextRole::Caption).w + 24.0f;
    if (!glyph_.empty()) width += 16.0f;
    if (closable_) width += kCloseW - 4.0f;
    return {std::max(width, 32.0f), 28.0f};
}

bool Chip::CloseHit(Point local) const noexcept {
    if (!closable_) return false;
    return local.x >= absolute_.w - kCloseW && local.x <= absolute_.w && local.y >= 0.0f &&
           local.y <= absolute_.h;
}

void Chip::Toggle() {
    if (!selectable_ || !enabled_) return;
    selected_ = !selected_;
    toggled_.Emit(selected_);
    Invalidate();
}

void Chip::Dismiss() {
    if (!closable_ || !enabled_) return;
    if (closed_.Empty()) Visible(false);
    else closed_.Emit();
}

void Chip::OnMouseEnter() {
    Control::OnMouseEnter();
    if (selectable_ || closable_) Animate();
}

void Chip::OnMouseLeave() {
    Control::OnMouseLeave();
    close_hover_ = false;
    if (selectable_ || closable_) Animate();
}

void Chip::OnMouseMove(Point local, uint32_t) {
    if (!closable_) return;
    const bool hit = CloseHit(local);
    if (hit != close_hover_) {
        close_hover_ = hit;
        Invalidate();
    }
}

void Chip::OnMouseDown(Point local, uint32_t buttons) {
    if (!enabled_ || !(buttons & kMkLeft)) return;
    if (CloseHit(local)) {
        Dismiss();
        return;
    }
    if (selectable_ || closable_) {
        pressed_ = true;
        Focus();
        Animate();
    }
}

void Chip::OnMouseUp(Point local, uint32_t) {
    const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
                        local.y <= absolute_.h;
    if (pressed_) {
        pressed_ = false;
        Animate();
    }
    if (enabled_ && inside && selectable_ && !CloseHit(local)) Toggle();
}

bool Chip::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (selectable_ && (vk == VK_SPACE || vk == VK_RETURN)) {
        Toggle();
        return true;
    }
    if (closable_ && vk == VK_BACK) {
        Dismiss();
        return true;
    }
    return false;
}

void Chip::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    if (selectable_ || closable_) Animate();
}

bool Chip::OnAnimate(float dt) {
    if (!selectable_ && !closable_) return false;
    const bool lit = (hovered_ || focused_) && enabled_;
    return EaseTo(glow_t_, lit ? 1.0f : 0.0f, dt, 14.0f) || Control::OnAnimate(dt);
}

CursorShape Chip::CursorAt(Point) const {
    return (selectable_ || closable_) ? CursorShape::Hand : CursorShape::Arrow;
}

void Chip::Draw(Painter& painter, const Theme& theme) {
    const float radius = absolute_.h * 0.5f;
    Color fg = foreground_.a > 0.0f ? foreground_ : theme.text_secondary;
    Color bg = custom_background_ ? background_ : theme.fill_hover;
    if (selectable_ || closable_) {
        if (selected_) {
            bg = theme.fill_selected;
            fg = theme.text;
            Color glow = theme.glow_sm;
            glow.a *= 0.28f * Lerp(0.45f, 1.0f, glow_t_) * theme.glow_intensity;
            if (glow.a > 0.004f) painter.DrawGlow(absolute_, radius, glow);
        } else {
            const float wash = 0.08f * glow_t_;
            bg = {Lerp(bg.r, 1.0f, wash), Lerp(bg.g, 1.0f, wash), Lerp(bg.b, 1.0f, wash),
                  bg.a};
            if (pressed_) bg = theme.fill_input_pressed;
        }
    }
    if (bg.a > 0.0f) painter.FillRoundedRect(absolute_, radius, bg);
    Color border = theme.control_stroke;
    if (selected_ && selectable_) {
        border = theme.accent;
        border.a = Lerp(0.35f, 0.85f, glow_t_) * theme.glow_intensity;
    }
    painter.StrokeRoundedRect(absolute_, radius, border);
    if (focused_ && enabled_ && (selectable_ || closable_)) {
        PaintFocusRing(painter, theme, absolute_, radius);
    }

    const float glyph_w = glyph_.empty() ? 0.0f : 16.0f;
    const float text_w = MeasureText(text_, TextRole::Caption).w;
    const float gap = glyph_w > 0.0f ? 6.0f : 0.0f;
    const float close = closable_ ? kCloseW : 0.0f;
    const float content_w = glyph_w + gap + text_w;
    const float x = absolute_.x + (absolute_.w - close - content_w) * 0.5f;
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, {x, absolute_.y, glyph_w, absolute_.h}, 16.0f, fg);
    }
    painter.DrawText(text_, {x + glyph_w + gap, absolute_.y, text_w, absolute_.h},
                     TextRole::Caption, fg);
    if (closable_) {
        const Rect close_r{absolute_.Right() - kCloseW, absolute_.y, kCloseW, absolute_.h};
        if (close_hover_) {
            painter.FillRoundedRect(close_r.Inset(3.0f, 5.0f), 8.0f, theme.fill_hover);
        }
        painter.DrawIcon(icon::kClose, close_r, 10.0f,
                         close_hover_ ? theme.text : theme.text_secondary);
    }
}

} // namespace lumen
