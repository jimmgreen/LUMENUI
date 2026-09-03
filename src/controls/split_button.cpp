#include "lumen/SplitButton.h"
#include "lumen/Icons.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace lumen {
namespace {
constexpr float kArrowZone = 32.0f;
constexpr float kPadX = 12.0f;
constexpr float kCheckSlot = 20.0f;   // 勾选字形 16 + 与文字间距 4
constexpr float kDotSlot = 16.0f;
} // namespace

void SplitButton::RelayoutParent() { Control::RelayoutParent(); }

void SplitButton::ToggleFromUser() {
    if (!toggle_) return;
    checked_ = !checked_;
    RelayoutParent();
    toggled_.Emit(checked_);
}

Size SplitButton::Measure(Size, const Theme& theme) {
    float width = kPadX + MeasureText(text_, TextRole::Body).w + kPadX + kArrowZone;
    if (toggle_ && checked_) width += kCheckSlot;
    else if (has_dot_) width += kDotSlot;
    return {std::max(width, 96.0f), theme.button_height};
}

void SplitButton::OnMouseEnter() {
    Control::OnMouseEnter();
    Animate();
}

void SplitButton::OnMouseLeave() {
    Control::OnMouseLeave();
    Animate();
}

bool SplitButton::OnAnimate(float dt) {
    const bool lit = (hovered_ || focused_) && enabled_;
    bool active = Control::OnAnimate(dt);
    active |= EaseTo(glow_t_, lit ? 1.0f : 0.0f, dt, 14.0f);
    return active;
}

void SplitButton::Draw(Painter& painter, const Theme& theme) {
    const Rect& r = absolute_;
    const float radius = theme.radius_control;

    Color fill{0, 0, 0, 0};
    Color border{0, 0, 0, 0};
    Color foreground = theme.text;
    float glow_strength = 0.0f;

    if (!enabled_) {
        fill = theme.fill_input_disabled;
        foreground = theme.text_disabled;
    } else if (primary_) {
        fill = pressed_ ? theme.accent_pressed : theme.accent;
        foreground = theme.primary_text;
        glow_strength = 0.9f;
    } else {
        fill = theme.fill_input;
        const float wash = 0.05f * glow_t_;
        fill = {Lerp(fill.r, 1.0f, wash), Lerp(fill.g, 1.0f, wash), Lerp(fill.b, 1.0f, wash),
                1.0f};
        if (pressed_) fill = theme.fill_input_pressed;
        border = Color{theme.accent.r, theme.accent.g, theme.accent.b,
                       Lerp(0.20f, 1.0f, glow_t_) * theme.glow_intensity};
        glow_strength = 0.55f;
    }

    if (glow_strength > 0.0f && glow_t_ > 0.004f) {
        painter.DrawGlow(r, radius, Color{theme.glow_lg.r, theme.glow_lg.g, theme.glow_lg.b,
                                          theme.glow_lg.a * glow_strength * glow_t_});
    }
    if (focused_ && enabled_) {
        // 聚焦辉光必须画在底色之前：DrawGlow 的角部径向块会盖住圆角内侧露灰块
        //（与 Toast 的"先外发光再填底"同一约束）；焦点环在内容之后补画。
        painter.DrawGlow(r, radius, Color{theme.glow_sm.r, theme.glow_sm.g, theme.glow_sm.b,
                                          theme.glow_sm.a * 0.7f});
    }
    painter.FillRoundedRect(r, radius, fill);
    if (!primary_ && enabled_) {
        painter.DrawInnerLight(r, radius, theme.edge_light, Color{0.0f, 0.0f, 0.0f, 0.30f});
    }
    if (border.a > 0.0f) painter.StrokeRoundedRect(r, radius, border);
    if (toggle_ && checked_ && !primary_) {
        // 开关选中态：整钮统一垫亮阶（只刷主区会灰黑拼色），分隔线与箭头叠在其上。
        painter.FillRoundedRect(r, radius, theme.fill_selected);
    }
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, r, radius);
    }

    // 分隔线：主区 | 箭头区（白底用文字色 25%，玻璃底用白 10%）
    const float divider_x = r.Right() - kArrowZone;
    const Color divider = primary_
                              ? Color{foreground.r, foreground.g, foreground.b,
                                      theme.control_stroke.a * 3.0f}
                              : theme.control_stroke;
    painter.FillRect({divider_x, r.y + 6.0f, 1.0f, r.h - 12.0f}, divider);

    // 主区内容
    float x = r.x + 12.0f;
    if (toggle_ && checked_) {
        painter.DrawIcon(icon::kCheckMark, {x, r.y, 16.0f, r.h}, 16.0f, foreground);
        x += 20.0f;
    } else if (has_dot_) {
        painter.FillRoundedRect({x, r.y + (r.h - 8.0f) * 0.5f, 8.0f, 8.0f}, 4.0f, dot_color_);
        x += 16.0f;
    }
    const Size text_size = MeasureText(text_, TextRole::Body);
    painter.DrawText(text_, {x, r.y, text_size.w, r.h}, TextRole::Body, foreground);

    // 箭头
    painter.DrawIcon(icon::kChevronDown, {divider_x, r.y, kArrowZone, r.h}, 10.0f, foreground);
}

bool SplitButton::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE || vk == VK_RETURN) {
        if (toggle_) {
            ToggleFromUser();
        } else {
            click_.Emit();
        }
        return true;
    }
    if (vk == VK_DOWN) {
        dropdown_.Emit();
        return true;
    }
    return false;
}

void SplitButton::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    arrow_pressed_ = local.x > absolute_.w - kArrowZone;
    pressed_ = true;
    Invalidate();
}

void SplitButton::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    const bool was_arrow = arrow_pressed_;
    arrow_pressed_ = false;
    pressed_ = false;
    const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
                        local.y <= absolute_.h;
    if (inside) {
        if (local.x > absolute_.w - kArrowZone || was_arrow) {
            dropdown_.Emit();
        } else if (toggle_) {
            ToggleFromUser();
        } else {
            click_.Emit();
        }
    }
    Invalidate();
}

void SplitButton::OnFocusChanged(bool focused) {
    focused_ = focused;
    Animate();
    Invalidate();
}

} // namespace lumen
