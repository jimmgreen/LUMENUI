#include "lumen/LogView.h"
#include "lumen/Clipboard.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <algorithm>

namespace lumen {
namespace {
constexpr float kBarHit = 10.0f;
} // namespace

void LogView::RelayoutParent() { Control::RelayoutParent(); }

LogView& LogView::ItemCount(size_t count) {
    item_count_ = count;
    if (selected_ >= static_cast<ptrdiff_t>(item_count_)) selected_ = -1;
    ClampScroll();
    if (follow_ && following_) ScrollToEnd();
    RelayoutParent();
    Invalidate();
    return *this;
}

LogView& LogView::SelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(item_count_)) return *this;
    selected_ = index;
    if (index >= 0) {
        const float row_h = RowHeight();
        const float top = static_cast<float>(index) * row_h;
        const float bottom = top + row_h;
        if (top < scroll_offset_) target_offset_ = top;
        else if (bottom > scroll_offset_ + absolute_.h) {
            target_offset_ = bottom - absolute_.h;
        }
        Animate();
    }
    Invalidate();
    return *this;
}

bool LogView::CopySelection() const {
    if (selected_ < 0 || !line_text_) return false;
    std::wstring line;
    line_text_(static_cast<size_t>(selected_), line);
    return clipboard::Text(line);
}

Size LogView::Measure(Size available, const Theme&) {
    const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 320.0f;
    const float h = (available.h > 0.0f && available.h < 1.0e4f) ? available.h : 180.0f;
    return {w, h};
}

float LogView::ContentHeight() const noexcept {
    return static_cast<float>(item_count_) * RowHeight();
}

float LogView::MaxScroll() const {
    return std::max(0.0f, ContentHeight() - absolute_.h);
}

void LogView::ClampScroll() {
    const float max_s = MaxScroll();
    target_offset_ = Clamp(target_offset_, 0.0f, max_s);
    scroll_offset_ = Clamp(scroll_offset_, 0.0f, max_s);
}

void LogView::ScrollToEnd() {
    target_offset_ = MaxScroll();
    if (!window_) scroll_offset_ = target_offset_;
    else Animate();
}

void LogView::PauseFollowIfScrolled() {
    if (!follow_) return;
    following_ = target_offset_ >= MaxScroll() - 2.0f;
}

ptrdiff_t LogView::RowAt(Point local) const {
    if (local.y < 0.0f || local.y >= absolute_.h) return -1;
    const ptrdiff_t row =
        static_cast<ptrdiff_t>((local.y + scroll_offset_) / std::max(RowHeight(), 1.0f));
    if (row < 0 || row >= static_cast<ptrdiff_t>(item_count_)) return -1;
    return row;
}

Rect LogView::VerticalTrack() const noexcept {
    return {absolute_.Right() - kBarHit, absolute_.y, kBarHit, absolute_.h};
}

ScrollThumb LogView::Thumb(float expand) const noexcept {
    return MakeScrollThumb(absolute_, ContentHeight(), scroll_offset_, expand, true);
}

bool LogView::BeginScrollDrag(Point local) {
    if (MaxScroll() <= 0.5f) return false;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (!VerticalTrack().Contains(world)) return false;
    const ScrollThumb thumb = Thumb(1.0f);
    dragging_ = true;
    following_ = false;
    if (thumb.visible && thumb.rect.Contains(world)) {
        drag_grab_ = world.y - thumb.rect.y;
    } else {
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, absolute_.h - thumb_h);
        const float t = Clamp((local.y - thumb_h * 0.5f) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        drag_grab_ = thumb_h * 0.5f;
        Invalidate();
    }
    Animate();
    return true;
}

bool LogView::CapturesOverlay(Point p) const {
    if (dragging_) return true;
    if (MaxScroll() <= 0.5f) return false;
    return VerticalTrack().Contains(p);
}

void LogView::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

bool LogView::OnAnimate(float dt_seconds) {
    bool moving = EaseTo(scroll_offset_, target_offset_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(expand_progress_, (hovered_ || dragging_) ? 1.0f : 0.0f, dt_seconds, 18.0f);
    if (moving) Invalidate();
    return moving || Control::OnAnimate(dt_seconds);
}

bool LogView::OnKey(uint32_t vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && vk == 'C') {
        CopySelection();
        return true;
    }
    const ptrdiff_t page =
        std::max(ptrdiff_t{1}, static_cast<ptrdiff_t>(absolute_.h / RowHeight()));
    switch (vk) {
    case VK_DOWN:
        SelectedIndex(selected_ < 0 ? 0 : std::min(selected_ + 1,
                                                      static_cast<ptrdiff_t>(item_count_) - 1));
        following_ = false;
        return true;
    case VK_UP:
        SelectedIndex(selected_ < 0 ? 0 : std::max(selected_ - 1, ptrdiff_t{0}));
        following_ = false;
        return true;
    case VK_NEXT:
        SelectedIndex(Clamp(selected_ + page, ptrdiff_t{0},
                               static_cast<ptrdiff_t>(item_count_) - 1));
        following_ = false;
        return true;
    case VK_PRIOR:
        SelectedIndex(Clamp(selected_ - page, ptrdiff_t{0},
                               static_cast<ptrdiff_t>(item_count_) - 1));
        following_ = false;
        return true;
    case VK_END:
        if (item_count_) {
            SelectedIndex(static_cast<ptrdiff_t>(item_count_) - 1);
            ScrollToEnd();
            following_ = follow_;
        }
        return true;
    case VK_HOME:
        if (item_count_) {
            SelectedIndex(0);
            target_offset_ = 0.0f;
            following_ = false;
            Animate();
        }
        return true;
    default:
        return false;
    }
}

void LogView::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    if (BeginScrollDrag(local)) return;
    const ptrdiff_t row = RowAt(local);
    if (row >= 0) SelectedIndex(row);
}

void LogView::OnMouseMove(Point local, uint32_t) {
    if (dragging_) {
        const ScrollThumb thumb = Thumb(1.0f);
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, absolute_.h - thumb_h);
        const float t = Clamp((local.y - drag_grab_) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        PauseFollowIfScrolled();
        Invalidate();
        return;
    }
    const ptrdiff_t row = RowAt(local);
    if (row != hover_row_) {
        hover_row_ = row;
        Animate();
        Invalidate();
    }
}

void LogView::OnMouseUp(Point, uint32_t) {
    dragging_ = false;
    PauseFollowIfScrolled();
    Animate();
}

void LogView::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_row_ = -1;
    Animate();
    Invalidate();
}

bool LogView::OnWheel(float delta) {
    if (MaxScroll() <= 0.0f) return false;
    target_offset_ = Clamp(target_offset_ - delta * RowHeight() * 3.0f, 0.0f, MaxScroll());
    PauseFollowIfScrolled();
    Animate();
    return true;
}

void LogView::Draw(Painter& painter, const Theme& theme) {
    ClampScroll();
    painter.PushClip(absolute_);
    painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);
    const float row_h = RowHeight();
    const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
    const ptrdiff_t visible = static_cast<ptrdiff_t>(absolute_.h / row_h) + 2;
    for (ptrdiff_t row = std::max(first, ptrdiff_t{0});
         row < first + visible && row < static_cast<ptrdiff_t>(item_count_); ++row) {
        const float y = absolute_.y + static_cast<float>(row) * row_h - scroll_offset_;
        const Rect slot{absolute_.x + 4.0f, y, absolute_.w - 8.0f, row_h};
        if (row == selected_) {
            painter.FillRoundedRect(slot, 4.0f, theme.fill_selected);
        } else if (row == hover_row_ && enabled_) {
            painter.FillRoundedRect(slot, 4.0f, theme.fill_hover);
        }
        draw_text_.clear();
        if (line_text_) line_text_(static_cast<size_t>(row), draw_text_);
        LogLevel level = LogLevel::Info;
        if (line_level_) level = line_level_(static_cast<size_t>(row));
        Color color = theme.text_secondary;
        if (level == LogLevel::Error) color = theme.text;
        else if (level == LogLevel::Warn) color = theme.text_secondary;
        else if (level == LogLevel::Debug) color = theme.text_disabled;
        else color = theme.text_secondary;
        if (level == LogLevel::Error) color = theme.text;
        painter.DrawText(draw_text_, {slot.x + 6.0f, slot.y, slot.w - 8.0f, slot.h}, TextRole::Mono,
                         color);
    }
    painter.PopClip();
    const bool hot = hovered_ || dragging_;
    const Color thumb = hot ? theme.scrollbar_thumb_hover : theme.scrollbar_thumb;
    painter.DrawScrollThumb(Thumb(std::max(expand_progress_, MaxScroll() > 0.5f ? 0.45f : 0.0f)),
                            thumb);
}

} // namespace lumen
