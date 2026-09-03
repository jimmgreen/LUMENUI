#include "lumen/Button.h"
#include "lumen/Command.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "lumen/ToolTip.h"
#include "../core/text_service.h"
#include <algorithm>
#include <cmath>

namespace lumen {

void Button::RelayoutParent() { Control::RelayoutParent(); }

void Button::RebindCommand() {
    cmd_changed_ = {};
    cmd_destroyed_ = {};
    if (!command_) return;
    cmd_changed_ = ScopedConnection(command_->OnChanged([this] {
        if (!command_) return;
        Enabled(command_->Enabled());
        text_ = command_->Label();
        glyph_ = command_->Glyph();
        RelayoutParent();
        Invalidate();
    }));
    cmd_destroyed_ = ScopedConnection(command_->OnDestroyed([this] {
        command_ = nullptr;
        cmd_changed_ = {};
        cmd_destroyed_ = {};
    }));
}

Button::Button(Button&& o) noexcept
    : ControlOf<Button>(std::move(o)),
      text_(std::move(o.text_)),
      glyph_(std::move(o.glyph_)),
      kind_(o.kind_),
      size_(o.size_),
      height_override_(o.height_override_),
      pill_(o.pill_),
      shimmer_(o.shimmer_),
      glow_t_(o.glow_t_),
      scale_t_(o.scale_t_),
      shimmer_angle_(o.shimmer_angle_),
      bloom_t_(o.bloom_t_),
      bloom_at_(o.bloom_at_),
      click_(std::move(o.click_)),
      command_(o.command_) {
    o.command_ = nullptr;
    o.cmd_changed_ = {};
    RebindCommand();
}

Button& Button::operator=(Button&& o) noexcept {
    if (this == &o) return *this;
    ControlOf<Button>::operator=(std::move(o));
    text_ = std::move(o.text_);
    glyph_ = std::move(o.glyph_);
    kind_ = o.kind_;
    size_ = o.size_;
    height_override_ = o.height_override_;
    pill_ = o.pill_;
    shimmer_ = o.shimmer_;
    glow_t_ = o.glow_t_;
    scale_t_ = o.scale_t_;
    shimmer_angle_ = o.shimmer_angle_;
    bloom_t_ = o.bloom_t_;
    bloom_at_ = o.bloom_at_;
    click_ = std::move(o.click_);
    command_ = o.command_;
    o.command_ = nullptr;
    o.cmd_changed_ = {};
    RebindCommand();
    return *this;
}

Button& Button::Bind(Command& command) {
    command_ = &command;
    if (text_.empty()) text_ = command.Label();
    if (glyph_.empty()) glyph_ = command.Glyph();
    Enabled(command.Enabled());
    RebindCommand();
    RelayoutParent();
    Invalidate();
    return *this;
}

namespace {

struct Metrics {
    float height;
    float pad_x;
};

Metrics SizeMetrics(ButtonSize size) {
    switch (size) {
    case ButtonSize::Small: return {40.0f, 12.0f};
    case ButtonSize::Large: return {44.0f, 24.0f};
    default: return {44.0f, 24.0f};
    }
}

constexpr float kPi = 3.14159265f;

TextRole ButtonTextRole(ButtonSize size, ButtonKind kind) {
    if (size == ButtonSize::Small) return TextRole::Caption;
    if (kind == ButtonKind::Primary || kind == ButtonKind::Danger) return TextRole::BodyStrong;
    return TextRole::Body;
}

} // namespace

Size Button::Measure(Size, const Theme& theme) {
    const Metrics m = SizeMetrics(size_);
    const float density = theme.button_height / 44.0f;
    const float min_h = height_override_ > 0.0f ? height_override_ : m.height * density;
    const TextRole text_role = ButtonTextRole(size_, kind_);
    if (glyph_.empty() && text_.empty()) return {min_h, min_h};
    if (!glyph_.empty() && text_.empty()) return {min_h, min_h};
    float width = m.pad_x * 2.0f;
    if (!glyph_.empty()) width += 16.0f + 6.0f;
    if (!text_.empty()) width += MeasureText(text_, text_role).w;
    if (pill_) width = std::max(width, min_h);
    return {std::max(width, min_h), min_h};
}

void Button::OnMouseEnter() {
    Control::OnMouseEnter();
    Animate();
}

void Button::OnMouseLeave() {
    Control::OnMouseLeave();
    Animate();
}

void Button::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    if (!enabled_) return;
    pressed_ = true;
    bloom_at_ = local;
    bloom_t_ = 1.0f;
    Animate();
}

void Button::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
                        local.y <= absolute_.h;
    if (pressed_) {
        pressed_ = false;
        Animate();
    }
    if (enabled_ && inside) {
        click_.Emit();
        if (command_) command_->Execute();
    }
}

bool Button::OnKey(uint32_t vk) {
    if (!enabled_) return false;
    if (vk == VK_SPACE || vk == VK_RETURN) {
        click_.Emit();
        if (command_) command_->Execute();
        return true;
    }
    return false;
}

void Button::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
}

bool Button::OnAnimate(float dt) {
    const bool lit = (hovered_ || focused_) && enabled_;
    bool active = Control::OnAnimate(dt);
    active |= EaseTo(glow_t_, lit ? 1.0f : 0.0f, dt, 14.0f);
    active |= EaseTo(scale_t_, pressed_ && enabled_ ? 1.0f : 0.0f, dt, 20.0f);
    active |= EaseTo(bloom_t_, 0.0f, dt, 8.0f);
    if (shimmer_ && lit && MotionScale() > 0.001f) {
        shimmer_angle_ += dt * (2.0f * kPi / 4.0f);
        active = true;
    }
    return active;
}

void Button::Draw(Painter& painter, const Theme& theme) {
    const TextRole text_role = ButtonTextRole(size_, kind_);
    const bool solid = kind_ == ButtonKind::Primary || kind_ == ButtonKind::Danger;
    const bool electric = shimmer_ && enabled_ && !solid;

    // 发光体不是实体键：悬停只增辉、绝不平移。按压才以中心轻微收缩给出触感。
    const float shrink = 0.02f * scale_t_;
    const Rect r = scale_t_ > 0.001f
                       ? absolute_.Inset(absolute_.w * shrink * 0.5f, absolute_.h * shrink * 0.5f)
                       : absolute_;
    const float radius = pill_ ? r.h * 0.5f : theme.radius_control;

    Color fill{0, 0, 0, 0};
    Color border{0, 0, 0, 0};
    Color foreground = theme.text;
    float rest_glow = 0.0f;
    float hover_glow = 0.0f;
    float glow_spread = 1.0f;

    switch (kind_) {
    case ButtonKind::Primary: {
        if (enabled_) {
            // 参考稿 rest/hover 都是白底；悬停只把 20px/0.3 光晕推到 35px/0.6。
            fill = pressed_ ? theme.accent_pressed : theme.accent;
            foreground = pressed_ ? theme.primary_text_pressed : theme.primary_text;
            rest_glow = 0.30f;
            hover_glow = 0.60f;
            glow_spread = Lerp(1.0f, 1.75f, glow_t_);
        } else {
            fill = theme.fill_input_disabled;
            foreground = theme.text_disabled;
        }
        break;
    }
    case ButtonKind::Danger: {
        if (enabled_) {
            fill = pressed_ ? theme.accent_pressed : theme.danger;
            foreground = theme.accent_text;
            rest_glow = 0.30f;
            hover_glow = 0.60f;
            glow_spread = Lerp(1.0f, 1.75f, glow_t_);
        } else {
            fill = theme.fill_input_disabled;
            foreground = theme.text_disabled;
        }
        break;
    }
    case ButtonKind::Standard: {
        if (enabled_) {
            fill = theme.fill_input;
            const float wash = 0.05f * glow_t_;
            fill = {Lerp(fill.r, 1.0f, wash), Lerp(fill.g, 1.0f, wash),
                    Lerp(fill.b, 1.0f, wash), 1.0f};
            if (pressed_) fill = theme.fill_input_pressed;
            border = Color{theme.accent.r, theme.accent.g, theme.accent.b,
                           Lerp(0.20f, 1.0f, glow_t_) * theme.glow_intensity};
            foreground = theme.text;
            hover_glow = 0.30f;
        } else {
            fill = theme.fill_input_disabled;
            border = Color{theme.accent.r, theme.accent.g, theme.accent.b,
                           0.08f * theme.glow_intensity};
            foreground = theme.text_disabled;
        }
        break;
    }
    case ButtonKind::Subtle:
    case ButtonKind::Transparent:
    default: {
        if (enabled_) {
            Color wash = theme.fill_pressed;
            wash.a *= glow_t_;
            fill = wash;
            foreground = {theme.text.r, theme.text.g, theme.text.b,
                          Lerp(theme.text_secondary.a, theme.text.a, glow_t_)};
            hover_glow = 0.0f;
        } else {
            foreground = theme.text_disabled;
        }
        break;
    }
    }

    if (electric) {
        fill = pressed_ ? theme.fill_input_pressed : theme.fill_input;
        border = {};
        rest_glow = 0.0f;
        hover_glow = 0.0f;
        foreground = {theme.text.r, theme.text.g, theme.text.b,
                      Lerp(0.90f, 1.0f, glow_t_)};
    }

    const float glow_a = Lerp(rest_glow, hover_glow, glow_t_) * theme.glow_intensity;
    if (glow_a > 0.004f) {
        if (pill_) {
            const float ring = 5.0f;
            Color halo = theme.glow_sm;
            halo.a = glow_a * 0.55f;
            painter.FillRoundedRect(r.Inset(-ring, -ring), radius + ring, halo);
        } else {
            painter.DrawGlow(r, radius, Color{theme.glow_sm.r, theme.glow_sm.g, theme.glow_sm.b, glow_a},
                             glow_spread, true);
        }
    }
    if (fill.a > 0.0f) painter.FillRoundedRect(r, radius, fill);
    if (bloom_t_ > 0.01f && enabled_) {
        const Point origin{r.x + bloom_at_.x, r.y + bloom_at_.y};
        const float rad = 70.0f + 110.0f * (1.0f - bloom_t_);
        const Color hot{1.0f, 1.0f, 1.0f, 0.16f * bloom_t_ * theme.glow_intensity};
        painter.FillRoundedRectRadial(r, radius, origin, rad, hot, Color{1.0f, 1.0f, 1.0f, 0.0f});
    }
    if (kind_ == ButtonKind::Standard && enabled_ && !pill_) {
        Color inset = theme.edge_light;
        inset.a *= Lerp(0.45f, 1.0f, glow_t_);
        painter.DrawInnerLight(r, radius, inset,
                               Color{0.0f, 0.0f, 0.0f, Lerp(0.22f, 0.40f, glow_t_)});
    }
    if (border.a > 0.0f) painter.StrokeRoundedRect(r, radius, border);
    if (shimmer_ && enabled_) {
        const float hot_a = theme.specular_line.a * Lerp(0.50f, 1.0f, glow_t_);
        const Color hot{theme.specular_line.r, theme.specular_line.g, theme.specular_line.b, hot_a};
        const Color tail{theme.specular_line.r, theme.specular_line.g, theme.specular_line.b, 0.0f};
        const Rect ring = r.Inset(-1.0f, -1.0f);
        painter.StrokeRoundedRectSweep(ring, radius + 1.0f, shimmer_angle_, hot, tail, 1.5f);
    }
    if (FocusVisible() && enabled_ && !solid) {
        PaintFocusRing(painter, theme, r, radius);
    }

    const float glyph_w = glyph_.empty() ? 0.0f : 16.0f;
    const float text_w = text_.empty() ? 0.0f : MeasureText(text_, text_role).w;
    const float gap = (glyph_w > 0.0f && text_w > 0.0f) ? 6.0f : 0.0f;
    // 槽比图标+文字窄时只画字形并居中（SplitView Compact 导航轨）。
    const bool show_text =
        !text_.empty() && (glyph_w <= 0.0f || glyph_w + gap + text_w <= r.w - 4.0f);
    const float content_w = show_text ? (glyph_w + gap + text_w) : glyph_w;
    const float left = r.x + (r.w - content_w) * 0.5f;
    float x = left;
    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, {x, r.y, glyph_w, r.h}, glyph_w, foreground);
        x += glyph_w + gap;
    }
    if (show_text) {
        // 参考稿 hover:text-shadow：Standard 与流光按钮悬停期文字提亮带晕。
        const bool text_glow = glow_t_ > 0.2f && (electric || kind_ == ButtonKind::Standard);
        if (text_glow) {
            painter.DrawTextGlow(text_, {x, r.y, text_w, r.h}, text_role, foreground);
        } else {
            painter.DrawText(text_, {x, r.y, text_w, r.h}, text_role, foreground);
        }
    }
}

} // namespace lumen
