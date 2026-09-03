#include "lumen/Pagination.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>

namespace lumen {
namespace {
constexpr float kButtonW = 32.0f;
constexpr float kButtonH = 28.0f;
constexpr float kGapX = 4.0f;
} // namespace

Pagination& Pagination::PageCount(size_t count) {
    count_ = std::max<size_t>(1, count);
    current_ = std::min(current_, count_);
    RelayoutParent();
    return *this;
}

Pagination& Pagination::Current(size_t page) {
    const size_t clamped = Clamp(page, size_t{1}, count_);
    if (current_ == clamped) return *this;
    current_ = clamped;
    RelayoutParent();
    return *this;
}

void Pagination::Navigate(size_t page) {
    const size_t clamped = Clamp(page, size_t{1}, count_);
    if (clamped == current_) return;
    current_ = clamped;
    RelayoutParent();
    navigate_.Emit(current_);
}

// 页码窗口：数量少全铺；多则 首末页 + 当前±1，间隙画省略号。
void Pagination::RebuildButtons(float width) {
    buttons_.clear();
    float x = kGapX;
    auto push = [&](int page, std::wstring label, float w = kButtonW) {
        buttons_.push_back({{x, 0.0f, w, kButtonH}, page, std::move(label)});
        x += w + kGapX;
    };
    push(-1, L"");   // 上一页
    const int n = static_cast<int>(count_);
    const int cur = static_cast<int>(current_);
    if (n <= 7) {
        for (int p = 1; p <= n; ++p) push(p, std::to_wstring(p));
    } else {
        push(1, L"1");
        const int lo = std::max(2, cur - 1);
        const int hi = std::min(n - 1, cur + 1);
        if (lo > 2) push(-3, L"…", kButtonW * 0.5f);
        for (int p = lo; p <= hi; ++p) push(p, std::to_wstring(p));
        if (hi < n - 1) push(-3, L"…", kButtonW * 0.5f);
        push(n, std::to_wstring(n));
    }
    push(-2, L"");   // 下一页
    (void)width;
}

Size Pagination::Measure(Size, const Theme&) {
    RebuildButtons(0.0f);
    const float w = buttons_.empty() ? 120.0f : buttons_.back().rect.Right() + kGapX;
    return {w, std::max(kButtonH, 28.0f)};
}

void Pagination::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    // hit 矩形全程局部坐标（输入用 local，绘制统一加 absolute_ 偏移），只做垂直居中。
    const float y = (absolute.h - kButtonH) * 0.5f;
    for (Hit& hit : buttons_) hit.rect.y = y;
}

bool Pagination::OnKey(uint32_t vk) {
    switch (vk) {
    case VK_LEFT:
        Navigate(current_ - 1);
        return true;
    case VK_RIGHT:
        Navigate(current_ + 1);
        return true;
    case VK_HOME:
        Navigate(1);
        return true;
    case VK_END:
        Navigate(count_);
        return true;
    default:
        return false;
    }
}

void Pagination::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    for (const Hit& hit : buttons_) {
        if (!hit.rect.Contains(local)) continue;
        if (hit.page == -1) Navigate(current_ - 1);
        else if (hit.page == -2) Navigate(current_ + 1);
        else if (hit.page > 0) Navigate(static_cast<size_t>(hit.page));
        return;
    }
}

void Pagination::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    int hover = -1;
    for (size_t i = 0; i < buttons_.size(); ++i) {
        if (buttons_[i].rect.Contains(local)) {
            hover = static_cast<int>(i);
            break;
        }
    }
    if (hover != hover_) {
        hover_ = hover;
        Invalidate();
    }
}

void Pagination::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_ != -1) {
        hover_ = -1;
        Invalidate();
    }
}

CursorShape Pagination::CursorAt(Point local) const {
    for (const Hit& hit : buttons_) {
        if (hit.rect.Contains(local)) return CursorShape::Hand;
    }
    return CursorShape::Arrow;
}

void Pagination::Draw(Painter& painter, const Theme& theme) {
    if (buttons_.empty() || absolute_.IsEmpty()) return;
    for (size_t i = 0; i < buttons_.size(); ++i) {
        const Hit& hit = buttons_[i];
        const Rect r{absolute_.x + hit.rect.x, absolute_.y + hit.rect.y, hit.rect.w, hit.rect.h};
        const bool hot = static_cast<int>(i) == hover_ && enabled_;
        if (hit.page == -3) {
            painter.DrawText(hit.label, r, TextRole::Body, theme.text_disabled, Align::Center);
            continue;
        }
        if (hit.page >= 1 && static_cast<size_t>(hit.page) == current_) {
            painter.FillRoundedRect(r, theme.radius_control * 0.6f, theme.fill_selected);
        } else if (hot) {
            painter.FillRoundedRect(r, theme.radius_control * 0.6f, theme.fill_hover);
        }
        if (hit.page == -1) {
            painter.DrawChevron({r.x + r.w * 0.5f, r.y + r.h * 0.5f}, 10.0f, 90.0f,
                                enabled_ ? theme.text_secondary : theme.text_disabled, 1.7f);
        } else if (hit.page == -2) {
            painter.DrawChevron({r.x + r.w * 0.5f, r.y + r.h * 0.5f}, 10.0f, -90.0f,
                                enabled_ ? theme.text_secondary : theme.text_disabled, 1.7f);
        } else {
            const bool current = static_cast<size_t>(hit.page) == current_;
            painter.DrawText(hit.label, r, TextRole::Body,
                             current || hot ? theme.text : theme.text_secondary, Align::Center);
        }
    }
}

} // namespace lumen
