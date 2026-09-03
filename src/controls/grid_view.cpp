#include "lumen/GridView.h"
#include "lumen/Painter.h"
#include "lumen/Animate.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace lumen {
namespace {
constexpr float kBarHit = 10.0f;
constexpr uint32_t kMkLeft = 0x0001;
constexpr float kEnterDur = 0.20f;
constexpr float kEnterStagger = 0.02f;
constexpr float kEnterLift = 8.0f;
constexpr float kEnterCap = 12.0f;

Color Fade(Color c, float t) noexcept {
    c.a *= t;
    return c;
}
} // namespace

void GridView::RelayoutParent() { Control::RelayoutParent(); }

GridView& GridView::Bind(ItemsModel& model) {
    owned_model_.reset();
    model_ = &model;
    model_cache_ = -1;
    item_text_ = [this](size_t i, std::wstring& s) {
        if (!model_) {
            s.clear();
            return;
        }
        if (model_cache_ != static_cast<ptrdiff_t>(i)) {
            model_->Get(i, model_row_);
            model_cache_ = static_cast<ptrdiff_t>(i);
        }
        s = model_row_.text;
    };
    item_glyph_ = [this](size_t i, std::wstring& s) {
        if (!model_) {
            s.clear();
            return;
        }
        if (model_cache_ != static_cast<ptrdiff_t>(i)) {
            model_->Get(i, model_row_);
            model_cache_ = static_cast<ptrdiff_t>(i);
        }
        s = model_row_.glyph;
    };
    auto sync = [this] {
        model_cache_ = -1;
        if (model_) ItemCount(model_->Count());
    };
    model_inserted_ = ScopedConnection(model.OnInserted([sync](size_t, size_t) { sync(); }));
    model_removed_ = ScopedConnection(model.OnRemoved([sync](size_t, size_t) { sync(); }));
    model_changed_ = ScopedConnection(model.OnChanged([this](size_t, size_t) {
        model_cache_ = -1;
        Invalidate();
    }));
    model_reset_ = ScopedConnection(model.OnReset(sync));
    model_detached_ = ScopedConnection(model.OnDetached([this] {
        model_ = nullptr;
        owned_model_.reset();
        ItemCount(0);
    }));
    ItemCount(model.Count());
    return *this;
}

GridView& GridView::Bind(std::shared_ptr<ItemsModel> model) {
    if (!model) return *this;
    ItemsModel& ref = *model;
    Bind(ref);
    owned_model_ = std::move(model);
    return *this;
}

GridView& GridView::ItemCount(size_t count) {
    item_count_ = count;
    if (selected_ >= static_cast<ptrdiff_t>(item_count_)) selected_ = -1;
    if (hover_ >= static_cast<ptrdiff_t>(item_count_)) hover_ = -1;
    ClampScroll();
    BeginEnter();
    RelayoutParent();
    return *this;
}

GridView& GridView::ItemSize(Size value) {
    item_size_ = {std::max(48.0f, value.w), std::max(48.0f, value.h)};
    RelayoutParent();
    return *this;
}

GridView& GridView::SelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(item_count_)) return *this;
    if (selected_ == index) return *this;
    selected_ = index;
    if (index >= 0) ScrollTo(static_cast<size_t>(index));
    Invalidate();
    selection_changed_.Emit(selected_, selected_);
    return *this;
}

int GridView::Columns() const {
    const float inner = std::max(0.0f, absolute_.w - 8.0f);
    const float stride = item_size_.w + item_gap_;
    if (stride <= 1.0f) return 1;
    return std::max(1, static_cast<int>((inner + item_gap_) / stride));
}

float GridView::ContentHeight() const {
    if (item_count_ == 0) return 0.0f;
    const int cols = Columns();
    const int rows = static_cast<int>((item_count_ + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
    return static_cast<float>(rows) * (item_size_.h + item_gap_) - item_gap_ + 8.0f;
}

float GridView::MaxScroll() const { return std::max(0.0f, ContentHeight() - absolute_.h); }

void GridView::ClampScroll() {
    const float max_s = MaxScroll();
    scroll_offset_ = Clamp(scroll_offset_, 0.0f, max_s);
    target_offset_ = Clamp(target_offset_, 0.0f, max_s);
}

void GridView::ScrollTo(size_t index) {
    const int cols = Columns();
    if (cols <= 0) return;
    const int row = static_cast<int>(index / static_cast<size_t>(cols));
    const float top = 4.0f + static_cast<float>(row) * (item_size_.h + item_gap_);
    const float bottom = top + item_size_.h;
    if (top < scroll_offset_) target_offset_ = top;
    else if (bottom > scroll_offset_ + absolute_.h) target_offset_ = bottom - absolute_.h;
    ClampScroll();
    Animate();
}

Size GridView::Measure(Size available, const Theme&) {
    const float width = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 280.0f;
    const float height = (available.h > 0.0f && available.h < 1.0e4f) ? available.h : 200.0f;
    return {width, height};
}

Rect GridView::VerticalTrack() const noexcept {
    return {absolute_.Right() - kBarHit, absolute_.y, kBarHit, absolute_.h};
}

ScrollThumb GridView::Thumb(float expand) const noexcept {
    return MakeScrollThumb(absolute_, ContentHeight(), scroll_offset_, expand, true);
}

bool GridView::CapturesOverlay(Point p) const {
    if (dragging_) return true;
    if (MaxScroll() <= 0.5f) return false;
    return p.x >= absolute_.w - kBarHit;
}

ptrdiff_t GridView::ItemAt(Point local) const {
    const int cols = Columns();
    if (cols <= 0 || item_count_ == 0) return -1;
    const float x = local.x - 4.0f;
    const float y = local.y - 4.0f + scroll_offset_;
    if (x < 0.0f || y < 0.0f) return -1;
    const float stride_x = item_size_.w + item_gap_;
    const float stride_y = item_size_.h + item_gap_;
    const int col = static_cast<int>(x / stride_x);
    const int row = static_cast<int>(y / stride_y);
    if (col < 0 || col >= cols) return -1;
    const float lx = x - static_cast<float>(col) * stride_x;
    const float ly = y - static_cast<float>(row) * stride_y;
    if (lx > item_size_.w || ly > item_size_.h) return -1;
    const ptrdiff_t index = static_cast<ptrdiff_t>(row) * cols + col;
    if (index < 0 || index >= static_cast<ptrdiff_t>(item_count_)) return -1;
    return index;
}

void GridView::OnFocusChanged(bool focused) {
    focused_ = focused;
    Invalidate();
}

void GridView::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_ = -1;
    dragging_ = false;
    Animate();
    Invalidate();
}

bool GridView::OnAnimate(float dt_seconds) {
    bool moving = EaseTo(scroll_offset_, target_offset_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(expand_progress_, (hovered_ || dragging_) ? 1.0f : 0.0f, dt_seconds, 18.0f);
    if (enter_playing_) {
        enter_elapsed_ += dt_seconds;
        if (enter_elapsed_ >= kEnterCap * kEnterStagger + kEnterDur) {
            enter_playing_ = false;
        } else {
            moving = true;
            Invalidate();
        }
    }
    return moving || Control::OnAnimate(dt_seconds);
}

void GridView::BeginEnter() {
    if (!window_ || MotionScale() <= 0.001f) {
        enter_playing_ = false;
        enter_elapsed_ = 10.0f;
        return;
    }
    enter_playing_ = true;
    enter_elapsed_ = 0.0f;
    Animate();
}

float GridView::ItemEnter(size_t visible_index) const noexcept {
    if (!enter_playing_) return 1.0f;
    const float delay = std::min(static_cast<float>(visible_index), kEnterCap) * kEnterStagger;
    return EaseAt(Clamp((enter_elapsed_ - delay) / kEnterDur, 0.0f, 1.0f), Ease::CssEaseOut);
}

bool GridView::OnWheel(float delta) {
    if (MaxScroll() <= 0.0f) return false;
    target_offset_ = Clamp(target_offset_ - delta * (item_size_.h + item_gap_), 0.0f, MaxScroll());
    Animate();
    return true;
}

bool GridView::OnKey(uint32_t vk) {
    if (!enabled_ || item_count_ == 0) return false;
    const int cols = Columns();
    ptrdiff_t next = selected_ < 0 ? 0 : selected_;
    switch (vk) {
    case VK_LEFT: next -= 1; break;
    case VK_RIGHT: next += 1; break;
    case VK_UP: next -= cols; break;
    case VK_DOWN: next += cols; break;
    case VK_HOME: next = 0; break;
    case VK_END: next = static_cast<ptrdiff_t>(item_count_) - 1; break;
    case VK_RETURN:
    case VK_SPACE:
        if (selected_ >= 0) activate_.Emit(static_cast<size_t>(selected_));
        return true;
    default: return false;
    }
    next = Clamp(next, ptrdiff_t{0}, static_cast<ptrdiff_t>(item_count_) - 1);
    SelectedIndex(next);
    return true;
}

void GridView::OnMouseDown(Point local, uint32_t buttons) {
    if (!enabled_ || !(buttons & kMkLeft)) return;
    Focus();
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (Thumb(1.0f).visible && VerticalTrack().Contains(world)) {
        dragging_ = true;
        const ScrollThumb thumb = Thumb(expand_progress_);
        if (thumb.rect.Contains(world)) {
            drag_grab_ = world.y - thumb.rect.y;
        } else {
            const float range = std::max(1.0f, absolute_.h - thumb.rect.h);
            const float t = Clamp((world.y - absolute_.y - thumb.rect.h * 0.5f) / range, 0.0f, 1.0f);
            target_offset_ = scroll_offset_ = t * MaxScroll();
            drag_grab_ = thumb.rect.h * 0.5f;
        }
        Animate();
        return;
    }
    const ptrdiff_t index = ItemAt(local);
    if (index >= 0) SelectedIndex(index);
}

void GridView::OnMouseDoubleClick(Point local) {
    const ptrdiff_t index = ItemAt(local);
    if (index >= 0) {
        SelectedIndex(index);
        activate_.Emit(selected_ >= 0 ? static_cast<size_t>(selected_) : 0);
    }
}

void GridView::OnMouseMove(Point local, uint32_t buttons) {
    if (dragging_ && (buttons & kMkLeft)) {
        const ScrollThumb thumb = Thumb(expand_progress_);
        const float range = std::max(1.0f, absolute_.h - thumb.rect.h);
        const float y = absolute_.y + local.y - drag_grab_;
        const float t = Clamp((y - absolute_.y) / range, 0.0f, 1.0f);
        target_offset_ = scroll_offset_ = t * MaxScroll();
        Invalidate();
        return;
    }
    const ptrdiff_t next = ItemAt(local);
    if (next != hover_) {
        hover_ = next;
        Invalidate();
    }
}

void GridView::OnMouseUp(Point, uint32_t) {
    dragging_ = false;
    Animate();
}

void GridView::Draw(Painter& painter, const Theme& theme) {
    ClampScroll();
    painter.PushClip(absolute_);
    painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);
    const int cols = Columns();
    if (cols > 0 && item_count_ > 0) {
        const float stride_x = item_size_.w + item_gap_;
        const float stride_y = item_size_.h + item_gap_;
        const int first_row = std::max(0, static_cast<int>(scroll_offset_ / stride_y));
        const int visible = static_cast<int>(absolute_.h / stride_y) + 2;
        const int last_row = first_row + visible;
        for (int row = first_row; row < last_row; ++row) {
            for (int col = 0; col < cols; ++col) {
                const ptrdiff_t index = static_cast<ptrdiff_t>(row) * cols + col;
                if (index < 0 || index >= static_cast<ptrdiff_t>(item_count_)) continue;
                const float x = absolute_.x + 4.0f + static_cast<float>(col) * stride_x;
                float y = absolute_.y + 4.0f + static_cast<float>(row) * stride_y - scroll_offset_;
                const size_t vis = static_cast<size_t>(
                    std::max(0, (row - first_row) * cols + col));
                const float t = ItemEnter(vis);
                if (t <= 0.004f) continue;
                y += kEnterLift * (1.0f - t);
                const Rect cell{x, y, item_size_.w, item_size_.h};
                if (cell.Bottom() < absolute_.y || cell.y > absolute_.Bottom()) continue;
                if (index == selected_) {
                    painter.FillRoundedRect(cell, theme.radius_control, Fade(theme.fill_selected, t));
                    painter.FillRoundedRect({cell.x, cell.y + 8.0f, 3.0f, cell.h - 16.0f}, 1.5f,
                                            Fade(theme.accent, t));
                } else if (index == hover_ && enabled_) {
                    painter.FillRoundedRect(cell, theme.radius_control, Fade(theme.fill_hover, t));
                }
                draw_glyph_.clear();
                draw_text_.clear();
                if (item_glyph_) item_glyph_(static_cast<size_t>(index), draw_glyph_);
                if (item_text_) item_text_(static_cast<size_t>(index), draw_text_);
                if (!draw_glyph_.empty()) {
                    painter.DrawIcon(draw_glyph_, {cell.x, cell.y + 10.0f, cell.w, 32.0f}, 24.0f,
                                     Fade(theme.text, t));
                }
                if (!draw_text_.empty()) {
                    painter.DrawText(draw_text_,
                                     {cell.x + 6.0f, cell.Bottom() - 28.0f, cell.w - 12.0f, 22.0f},
                                     TextRole::Caption, Fade(theme.text_secondary, t), Align::Center);
                }
                if (focused_ && index == selected_) {
                    PaintFocusRing(painter, theme, cell.Inset(2.0f, 2.0f), theme.radius_control);
                }
            }
        }
    }
    const Color thumb = (hovered_ || dragging_) ? theme.scrollbar_thumb_hover : theme.scrollbar_thumb;
    painter.DrawScrollThumb(Thumb(expand_progress_), thumb);
    painter.PopClip();
}

} // namespace lumen
