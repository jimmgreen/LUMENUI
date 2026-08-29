#include "fluentui/ListView.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace fui {

void ListView::RelayoutParent() { Control::RelayoutParent(); }

float ListView::MaxScroll() const {
    const float content = static_cast<float>(item_count_) * std::max(theme_row_height_, 1.0f);
    return std::max(0.0f, content - absolute_.h);
}

void ListView::SetSelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(item_count_)) return;
    if (selected_ == index) return;
    selected_ = index;
    keyboard_anchor_ = index;
    if (index >= 0) ScrollTo(static_cast<size_t>(index));
    Invalidate();
    if (selection_changed_) selection_changed_();
}

void ListView::ScrollTo(size_t index) {
    if (absolute_.IsEmpty()) return;   // 首次布局前没有视口尺寸
    const float row_h = std::max(theme_row_height_, 1.0f);
    const float top = static_cast<float>(index) * row_h;
    const float bottom = top + row_h;
    if (top < scroll_offset_) {
        target_offset_ = top;
    } else if (bottom > scroll_offset_ + absolute_.h) {
        target_offset_ = bottom - absolute_.h;
    }
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    Animate();
}

void ListView::ClampScroll() {
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    scroll_offset_ = Clamp(scroll_offset_, 0.0f, MaxScroll());
}

Size ListView::Measure(Size, const Theme&) {
    return {200.0f, 200.0f};
}

void ListView::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

bool ListView::OnAnimate(float dt_seconds) {
    bool moving = EaseTo(scroll_offset_, target_offset_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(expand_progress_, hovered_ ? 1.0f : 0.0f, dt_seconds, 18.0f);
    return moving;
}

void ListView::MoveSelection(ptrdiff_t delta) {
    if (item_count_ == 0) return;
    const ptrdiff_t next =
        selected_ < 0 ? (delta > 0 ? 0 : static_cast<ptrdiff_t>(item_count_) - 1)
                      : Clamp(selected_ + delta, ptrdiff_t{0},
                              static_cast<ptrdiff_t>(item_count_) - 1);
    if (next == selected_) return;
    selected_ = next;
    keyboard_anchor_ = next;
    ScrollTo(static_cast<size_t>(next));
    Invalidate();
    if (selection_changed_) selection_changed_();
}

bool ListView::OnKey(uint32_t vk) {
    switch (vk) {
    case VK_DOWN:
        MoveSelection(1);
        return true;
    case VK_UP:
        MoveSelection(-1);
        return true;
    case VK_HOME:
        if (item_count_) {
            selected_ = 0;
            keyboard_anchor_ = 0;
            target_offset_ = 0.0f;
            Animate();
            Invalidate();
            if (selection_changed_) selection_changed_();
        }
        return true;
    case VK_END:
        if (item_count_) {
            selected_ = static_cast<ptrdiff_t>(item_count_) - 1;
            keyboard_anchor_ = selected_;
            target_offset_ = MaxScroll();
            Animate();
            Invalidate();
            if (selection_changed_) selection_changed_();
        }
        return true;
    case VK_RETURN:
        if (selected_ >= 0 && activate_) activate_();
        return true;
    default:
        return false;
    }
}

ptrdiff_t ListView::RowAt(Point local) const {
    const float row_h = std::max(theme_row_height_, 1.0f);
    const float y = local.y + scroll_offset_;
    if (local.x < 0.0f || local.x > absolute_.w || y < 0.0f) return -1;
    const ptrdiff_t row = static_cast<ptrdiff_t>(y / row_h);
    if (row >= static_cast<ptrdiff_t>(item_count_)) return -1;
    return row;
}

void ListView::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    const ptrdiff_t row = RowAt(local);
    if (row >= 0) SetSelectedIndex(row);
}

void ListView::OnMouseDoubleClick(Point local) {
    if (RowAt(local) >= 0 && activate_) activate_();
}

void ListView::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    const ptrdiff_t row = RowAt(local);
    if (row != hover_row_) {
        hover_row_ = row;
        Animate();   // 驱动滚动条展开动画
        Invalidate();
    }
}

void ListView::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_row_ = -1;
    Animate();
    Invalidate();
}

void ListView::OnWheel(float delta) {
    if (item_count_ == 0) return;
    const float row_h = std::max(theme_row_height_, 1.0f);
    target_offset_ = Clamp(target_offset_ - delta * row_h * 2.5f, 0.0f, MaxScroll());
    Animate();
}

void ListView::Draw(Painter& painter, const Theme& theme) {
    theme_row_height_ = theme.list_row_height;
    const float row_h = std::max(theme_row_height_, 1.0f);
    ClampScroll();
    painter.PushClip(absolute_);
    painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);

    const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
    const ptrdiff_t visible = static_cast<ptrdiff_t>(absolute_.h / row_h) + 2;
    for (ptrdiff_t row = first;
         row < first + visible && row < static_cast<ptrdiff_t>(item_count_); ++row) {
        const Rect row_rect{absolute_.x,
                            absolute_.y + static_cast<float>(row) * row_h - scroll_offset_,
                            absolute_.w, row_h};
        // 行底：左右各内缩 4、圆角 8；选中 = fill_selected + 左侧 3px accent 竖条
        const Rect row_slot = row_rect.Inset(4.0f, 0.0f);
        if (row == selected_) {
            painter.FillRoundedRect(row_slot, theme.radius_control, theme.fill_selected);
            painter.FillRoundedRect({row_slot.x, row_slot.y, 3.0f, row_slot.h}, 1.5f,
                                    theme.accent);
        } else if (row == hover_row_ && enabled_) {
            painter.FillRoundedRect(row_slot, theme.radius_control, theme.fill_hover);
        }
        const std::wstring text =
            item_text_ ? item_text_(static_cast<size_t>(row)) : std::wstring();
        const std::wstring glyph =
            item_glyph_ ? item_glyph_(static_cast<size_t>(row)) : std::wstring();
        float text_x = absolute_.x + 12.0f;
        if (!glyph.empty()) {
            painter.DrawIcon(glyph, {text_x, row_rect.y, 16.0f, row_rect.h}, 15.0f,
                             theme.text_secondary);
            text_x += 26.0f;
        }
        if (!text.empty()) {
            painter.DrawText(text, {text_x, row_rect.y, absolute_.Right() - 12.0f - text_x,
                                    row_rect.h},
                             TextRole::Body, theme.text);
        }
    }

    // 滚动条：2.5px 圆头滑块（悬停展开到 5px 由 expand_progress 驱动）
    const float content = static_cast<float>(item_count_) * row_h;
    if (content > absolute_.h + 0.5f) {
        const float thumb_w = 2.5f + 2.5f * expand_progress_;
        const float thumb_h = std::max(20.0f, absolute_.h * absolute_.h / content);
        const float track_h = absolute_.h - thumb_h;
        const float range = content - absolute_.h;
        const float thumb_y =
            absolute_.y + (range > 0.0f ? scroll_offset_ / range * track_h : 0.0f);
        painter.FillRoundedRect(
            {absolute_.Right() - 2.5f - thumb_w, thumb_y, thumb_w, thumb_h}, thumb_w * 0.5f,
            theme.scrollbar_thumb);
    }
    painter.PopClip();
    if (focused_ && enabled_) {
        painter.DrawFocusRing(absolute_, theme.radius_control, theme.accent,
                              theme.focus_ring_width);
    }
}

} // namespace fui
