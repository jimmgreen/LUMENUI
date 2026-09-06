#include "lumen/SettingsCard.h"
#include "lumen/Painter.h"
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {
// Measure / Arrange / Draw 共用同一几何，避免三处各算各的导致文字区漂移。
constexpr float kPad = 14.0f;
constexpr float kGlyphSlot = 40.0f;   // 含右间距
constexpr float kTailGap = 12.0f;     // 尾部控件间距
constexpr float kTextTailGap = 8.0f;  // 文本区与尾部的留白
constexpr float kMinTextW = 60.0f;
constexpr float kTitleGap = 2.0f;
} // namespace

void SettingsCard::RelayoutParent() { Control::RelayoutParent(); }

void SettingsCard::ComputeGeometry(float width) {
    pad_ = compact_ ? 8.0f : kPad;
    const float inner = std::max(0.0f, width - pad_ * 2.0f);
    const float glyph = glyph_.empty() ? 0.0f : std::min(kGlyphSlot, inner);
    float tail_width = 0.0f, tail_height = 0.0f;
    for (size_t i = 0; i < ChildCount(); ++i) if (ChildVisible(i)) {
        tail_width += (tail_width > 0.0f ? kTailGap : 0.0f) + std::min(inner, ChildDesired(i).w);
        tail_height = std::max(tail_height, ChildDesired(i).h);
    }
    const bool below = tail_width > 0.0f && inner - glyph - tail_width - kTextTailGap < kMinTextW;
    text_left_ = pad_ + glyph;
    text_width_ = std::max(0.0f, inner - glyph - (!below && tail_width > 0.0f ? tail_width + kTextTailGap : 0.0f));
    title_height_ = title_.empty() ? 0.0f : MeasureWrapped(title_, TextRole::BodyStrong, std::max(1.0f, text_width_));
    text_height_ = title_height_;
    if (!description_.empty()) text_height_ += (title_.empty() ? 0.0f : kTitleGap) +
        MeasureWrapped(description_, TextRole::Caption, std::max(1.0f, text_width_));
    text_height_ = std::max(text_height_, glyph > 0.0f ? 32.0f : 0.0f);
    tail_bounds_.resize(ChildCount());
    float x = below ? pad_ : width - pad_ - tail_width;
    float y = below ? pad_ + text_height_ + kTextTailGap : pad_;
    float row_height = 0.0f;
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (!ChildVisible(i)) { tail_bounds_[i] = {}; continue; }
        const Size d = ChildDesired(i);
        const float w = std::min(inner, d.w);
        if (below && x > pad_ && x + w > width - pad_) {
            x = pad_; y += row_height + kTextTailGap; row_height = 0.0f;
        }
        tail_bounds_[i] = {x, below ? y : pad_ + (std::max(text_height_, tail_height) - d.h) * 0.5f, w, d.h};
        x += w + kTailGap;
        row_height = std::max(row_height, d.h);
    }
    layout_height_ = below ? y + row_height + pad_ : std::max(text_height_, tail_height) + pad_ * 2.0f;
}

Size SettingsCard::Measure(Size available, const Theme& theme) {
    const float width = std::isfinite(available.w) && available.w < 1.0e4f ? std::max(1.0f, available.w) : 320.0f;
    for (size_t i = 0; i < ChildCount(); ++i) if (ChildVisible(i))
        MeasureChildAt(i, {std::max(1.0f, width - (compact_ ? 16.0f : 28.0f)), 1.0e5f}, theme);
    ComputeGeometry(width);
    return {width, layout_height_};
}

void SettingsCard::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    ComputeGeometry(absolute.w);
    for (size_t i = 0; i < ChildCount(); ++i) if (ChildVisible(i)) {
        SetChildBounds(Child(i), tail_bounds_[i]);
        ArrangeChildAt(i);
    }
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
    if (accent_bar_) {
        painter.DrawGlow(bar, 1.5f, theme.glow_sm);
        painter.FillRoundedRect(bar, 1.5f, theme.accent);
    }

    if (!glyph_.empty()) {
        const Rect glyph_box{absolute_.x + pad_, absolute_.y + pad_, 32.0f,
                             32.0f};
        const Color glyph_bg = glyph_bg_.a > 0.0f ? glyph_bg_ : theme.fill_hover;
        painter.FillRoundedRect(glyph_box, 8.0f, glyph_bg);
        painter.StrokeRoundedRect(glyph_box, 8.0f, theme.control_stroke);
        painter.DrawIcon(glyph_, glyph_box, 16.0f,
                         glyph_bg_.a > 0.0f ? Color::Hex(0xFFFFFF) : theme.text);
    }

    const Color text_color = enabled_ ? theme.text : theme.text_disabled;
    float y = absolute_.y + pad_;
    if (!title_.empty()) {
        painter.DrawTextWrapped(title_, {absolute_.x + text_left_, y, text_width_, title_height_}, TextRole::BodyStrong,
                         text_color);
        y += title_height_;
    }
    if (!description_.empty()) {
        if (!title_.empty()) y += kTitleGap;
        painter.DrawTextWrapped(description_, {absolute_.x + text_left_, y, text_width_, absolute_.y + pad_ + text_height_ - y},
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
