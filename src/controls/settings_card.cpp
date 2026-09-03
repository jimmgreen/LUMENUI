#include "lumen/SettingsCard.h"
#include "lumen/Painter.h"
#include <algorithm>

namespace lumen {

void SettingsCard::RelayoutParent() { Control::RelayoutParent(); }

Size SettingsCard::Measure(Size available, const Theme& theme) {
    const float pad = 14.0f;
    const float glyph_w = glyph_.empty() ? 0.0f : 40.0f;
    // 尾部子项（如 Switch）靠右排布
    float tail_w = 0.0f, tail_h = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {1.0e5f, 1.0e5f}, theme);
        tail_w += desired.w + (tail_w > 0.0f ? 12.0f : 0.0f);
        tail_h = std::max(tail_h, desired.h);
    }
    const float text_block = glyph_.empty() ? 0.0f : 8.0f;   // glyph 与文本间距
    const float text_w = available.w - pad * 2.0f - glyph_w - text_block - tail_w - 16.0f;
    float content_h = 20.0f;
    if (!title_.empty()) {
        content_h = MeasureText(title_, TextRole::BodyStrong).h;
    }
    if (!description_.empty() && text_w > 60.0f) {
        content_h += 2.0f + MeasureWrapped(description_, TextRole::Caption, text_w);
    }
    return {std::max(available.w, 240.0f),
            std::max(content_h, tail_h) + pad * 2.0f};
}

void SettingsCard::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const float pad = 14.0f;
    const float glyph_w = glyph_.empty() ? 0.0f : 40.0f;

    // 尾部子项靠右、垂直居中
    float tail_total = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        tail_total += ChildDesired(i).w + 12.0f;
    }
    if (tail_total > 0.0f) tail_total -= 12.0f;
    float x = absolute.Right() - pad - tail_total;
    const float cy = absolute.y + absolute.h * 0.5f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = ChildDesired(i);
        SetChildBounds(Child(i), {x - absolute.x, cy - desired.h * 0.5f - absolute.y, desired.w,
                                  desired.h});
        ArrangeChildAt(i);
        x += desired.w + 12.0f;
    }
    text_left_ = absolute.x + pad + glyph_w + (glyph_.empty() ? 0.0f : 8.0f);
    text_width_ = std::max(60.0f, x - text_left_ - 4.0f);
}

void SettingsCard::Draw(Painter& painter, const Theme& theme) {
    // 碳底 + 可选聚光（Spotlight 显式开启）：光斑渐显时含边缘折射光环与描边
    const Color fill = enabled_ && pressed_ ? theme.fill_input_pressed : theme.fill_input;
    painter.FillRoundedRect(absolute_, theme.radius_card, fill);
    if (spotlight_t_ > 0.004f) {
        DrawSpotlight(painter, theme, absolute_, theme.radius_card, SpotlightCenter(),
                      spotlight_t_);
    } else {
        painter.StrokeRoundedRect(absolute_, theme.radius_card, theme.stroke_card);
    }

    const Rect bar{absolute_.x, absolute_.y + 10.0f, 3.0f, absolute_.h - 20.0f};
    painter.DrawGlow(bar, 1.5f, theme.glow_sm);
    painter.FillRoundedRect(bar, 1.5f, theme.accent);

    float x = absolute_.x + 18.0f;
    if (!glyph_.empty()) {
        const Rect glyph_box{x, absolute_.y + (absolute_.h - 32.0f) * 0.5f, 32.0f, 32.0f};
        const Color glyph_bg = glyph_bg_.a > 0.0f ? glyph_bg_ : theme.fill_hover;
        painter.FillRoundedRect(glyph_box, 8.0f, glyph_bg);
        painter.StrokeRoundedRect(glyph_box, 8.0f, theme.control_stroke);
        painter.DrawIcon(glyph_, glyph_box, 16.0f,
                         glyph_bg_.a > 0.0f ? Color::Hex(0xFFFFFF) : theme.text);
        x += 40.0f;
    }

    const Color text_color = enabled_ ? theme.text : theme.text_disabled;
    float y = absolute_.y + 14.0f;
    if (!title_.empty()) {
        painter.DrawText(title_, {x, y, text_width_, 20.0f}, TextRole::BodyStrong, text_color);
        y += 20.0f;
    }
    if (!description_.empty()) {
        if (!title_.empty()) y += 2.0f;
        painter.DrawTextWrapped(description_, {x, y, text_width_, absolute_.Bottom() - y - 12.0f},
                                TextRole::Caption, theme.text_secondary);
    }
    if (HasFocus()) {
        PaintFocusRing(painter, theme, absolute_, theme.radius_card);
    }
}

void SettingsCard::OnMouseEnter() {
    Control::OnMouseEnter();
}

void SettingsCard::OnMouseLeave() {
    hovered_ = false;
    pressed_ = false;
    Invalidate();
}

void SettingsCard::OnMouseDown(Point local, uint32_t buttons) {
    (void)local;
    (void)buttons;
    // 命中在尾部子项上的事件不会到达这里（子项更顶层）
    pressed_ = true;
    Invalidate();
}

void SettingsCard::OnMouseUp(Point local, uint32_t buttons) {
    (void)buttons;
    pressed_ = false;
    const bool inside = local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w &&
                        local.y <= absolute_.h;
    if (inside && enabled_) click_.Emit();
    Invalidate();
}

bool SettingsCard::OnKey(uint32_t vk) {
    if (click_.Empty()) return false;
    if (vk == VK_SPACE || vk == VK_RETURN) {
        click_.Emit();
        return true;
    }
    return false;
}

void SettingsCard::OnFocusChanged(bool focused) {
    (void)focused;
    Invalidate();
}

} // namespace lumen
