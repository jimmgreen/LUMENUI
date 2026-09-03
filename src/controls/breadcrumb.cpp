#include "lumen/Breadcrumb.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>

namespace lumen {
namespace {
constexpr float kPadX = 8.0f;        // 首段左缩进
constexpr float kSegGap = 26.0f;     // 段与段之间的空隙（chevron 居中）
constexpr float kPillPadX = 10.0f;   // 悬停胶囊左右内边距
constexpr float kPillInsetY = 5.0f;  // 悬停胶囊上下内边距
} // namespace

Breadcrumb& Breadcrumb::SelectedIndex(ptrdiff_t index) {
    const ptrdiff_t clamped = index >= static_cast<ptrdiff_t>(items_.size()) ? -1 : index;
    if (selected_ == clamped) return *this;
    selected_ = clamped;
    Invalidate();
    return *this;
}

void Breadcrumb::RefreshCache() const {
    if (!layout_cache_dirty_) return;
    segment_x_.clear();
    segment_w_.clear();
    float x = kPadX;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float w = MeasureText(items_[i], TextRole::Body).w;
        segment_x_.push_back(x);
        segment_w_.push_back(w);
        x += w + kSegGap;
    }
    layout_cache_dirty_ = false;
}

void Breadcrumb::Navigate(size_t index) {
    if (index >= items_.size()) return;
    selected_ = static_cast<ptrdiff_t>(index);
    Invalidate();
    navigate_.Emit(index);
}

Size Breadcrumb::Measure(Size, const Theme& theme) {
    RefreshCache();
    const float w =
        items_.empty() ? 80.0f : segment_x_.back() + segment_w_.back() + kPadX;
    return {w, theme.input_height};
}

bool Breadcrumb::OnKey(uint32_t vk) {
    if (items_.empty()) return false;
    const ptrdiff_t last = static_cast<ptrdiff_t>(items_.size()) - 1;
    switch (vk) {
    case VK_LEFT:
        SelectedIndex(selected_ < 0 ? last : std::max<ptrdiff_t>(0, selected_ - 1));
        return true;
    case VK_RIGHT:
        SelectedIndex(selected_ < 0 ? 0 : std::min(last, selected_ + 1));
        return true;
    case VK_RETURN:
    case VK_SPACE:
        if (selected_ >= 0) {
            Navigate(static_cast<size_t>(selected_));
            return true;
        }
        return false;
    case VK_HOME:
        SelectedIndex(0);
        return true;
    case VK_END:
        SelectedIndex(last);
        return true;
    default:
        return false;
    }
}

void Breadcrumb::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001) || items_.empty()) return;
    Focus();
    RefreshCache();   // 布局未跑（纯逻辑测试）时也能命中
    for (size_t i = 0; i < items_.size(); ++i) {
        if (local.x >= segment_x_[i] - kPillPadX &&
            local.x <= segment_x_[i] + segment_w_[i] + kPillPadX) {
            Navigate(i);
            return;
        }
    }
}

void Breadcrumb::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    RefreshCache();
    ptrdiff_t hover = -1;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (local.x >= segment_x_[i] - kPillPadX &&
            local.x <= segment_x_[i] + segment_w_[i] + kPillPadX) {
            hover = static_cast<ptrdiff_t>(i);
            break;
        }
    }
    if (hover != hover_) {
        hover_ = hover;
        Invalidate();
    }
}

void Breadcrumb::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_ != -1) {
        hover_ = -1;
        Invalidate();
    }
}

void Breadcrumb::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

CursorShape Breadcrumb::CursorAt(Point local) const {
    if (layout_cache_dirty_) return CursorShape::Arrow;   // const 路径不重建；Measure 已建
    for (size_t i = 0; i < items_.size(); ++i) {
        if (local.x >= segment_x_[i] - kPillPadX &&
            local.x <= segment_x_[i] + segment_w_[i] + kPillPadX) {
            return CursorShape::Hand;
        }
    }
    return CursorShape::Arrow;
}

void Breadcrumb::Draw(Painter& painter, const Theme& theme) {
    if (items_.empty() || absolute_.IsEmpty()) return;
    RefreshCache();
    for (size_t i = 0; i < items_.size(); ++i) {
        const bool selected = static_cast<ptrdiff_t>(i) == selected_;
        const bool hot = static_cast<ptrdiff_t>(i) == hover_ && enabled_;
        // 悬停才出现胶囊（Fluent 惯例）；当前项只靠亮字与次级文本分层，不垫持久色块。
        if (hot) {
            const Rect pill{absolute_.x + segment_x_[i] - kPillPadX, absolute_.y + kPillInsetY,
                            segment_w_[i] + kPillPadX * 2.0f,
                            absolute_.h - kPillInsetY * 2.0f};
            painter.FillRoundedRect(pill, pill.h * 0.5f, theme.fill_hover);
        }
        const Color color = (selected || hot) && enabled_ ? theme.text : theme.text_secondary;
        painter.DrawText(items_[i], {absolute_.x + segment_x_[i], absolute_.y,
                                     segment_w_[i] + kPillPadX * 2.0f, absolute_.h},
                         TextRole::Body, color);
        if (i + 1 < items_.size()) {
            // chevron 居中于段间隙；参数与 TreeView 展开箭头一致，避免小字形发虚。
            painter.DrawChevron({absolute_.x + segment_x_[i] + segment_w_[i] + kSegGap * 0.5f,
                                 absolute_.y + absolute_.h * 0.5f},
                                11.0f, -90.0f, theme.text_secondary, 1.7f);
        }
    }
}

} // namespace lumen
