#include "lumen/Carousel.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace lumen {

Carousel::Carousel() {}

Carousel& Carousel::Current(size_t index) {
    const size_t clamped = Clamp(index, size_t{0}, PageCount() == 0 ? size_t{0} : PageCount() - 1);
    if (current_ == clamped) return *this;
    current_ = clamped;
    Animate();
    return *this;
}

void Carousel::Navigate(size_t index) {
    const size_t before = current_;
    Current(index);
    if (current_ != before) page_changed_.Emit(current_);
}

void Carousel::Place() {
    if (absolute_.IsEmpty()) return;
    for (size_t i = 0; i < ChildCount(); ++i) {
        Control& page = Child(i);
        // bounds_ 是相对父级的：偏移量按页宽，最终绝对位置由 ArrangeChildAt 换算。
        SetChildBounds(page, {(static_cast<float>(i) - slide_) * absolute_.w, 0.0f,
                              absolute_.w, absolute_.h});
        ArrangeChildAt(i);
    }
}

Size Carousel::Measure(Size available, const Theme& theme) {
    // 页固定铺满（尺寸来自 Arrange），但必须向下测量：StackPanel::Arrange 依据
    // 子级的 desired_ 缓存定位，跳过测量会导致页内所有内容按 0 尺寸摆放（空白页）。
    for (size_t i = 0; i < ChildCount(); ++i) {
        MeasureChildAt(i, {available.w, available.h}, theme);
    }
    return {320.0f, 180.0f};
}

void Carousel::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    slide_ = static_cast<float>(current_);   // 布局尺寸变化后直接贴合当前页
    Place();
}

Rect Carousel::DotStrip() const noexcept {
    // 局部坐标（与输入回调/光标约定一致）；绘制时自行叠加 absolute_。
    return {0.0f, absolute_.h - 20.0f, absolute_.w, 20.0f};
}

bool Carousel::OnAnimate(float dt_seconds) {
    if (EaseTo(slide_, static_cast<float>(current_), dt_seconds, 14.0f, 0.002f)) {
        Place();
        Invalidate();
        return true;
    }
    return Control::OnAnimate(dt_seconds);
}

bool Carousel::OnWheel(float delta) {
    if (PageCount() == 0) return false;
    const size_t next = delta < 0 ? current_ + 1 : current_ - 1;
    if (next == current_) return false;
    Navigate(next);
    return true;
}

bool Carousel::OnKey(uint32_t vk) {
    if (vk == VK_LEFT) {
        Navigate(current_ == 0 ? 0 : current_ - 1);
        return true;
    }
    if (vk == VK_RIGHT) {
        Navigate(std::min(PageCount() == 0 ? size_t{0} : PageCount() - 1, current_ + 1));
        return true;
    }
    return false;
}

int Carousel::DotAt(Point local) const {
    if (!dots_ || PageCount() < 2) return -1;
    const Rect strip = DotStrip();
    if (!strip.Contains(local)) return -1;
    // 圆点间距 14，从中心向两侧排开；整格可点（不必精确点到 6px 圆点上）。
    const float total = static_cast<float>(PageCount() - 1) * 14.0f;
    const float first = strip.x + (strip.w - total) * 0.5f - 3.0f;
    const int index = static_cast<int>((local.x - first + 6.0f) / 14.0f);
    if (index < 0 || index >= static_cast<int>(PageCount())) return -1;
    return index;
}

bool Carousel::CapturesOverlay(Point p) const {
    // 命中树传入窗口绝对坐标；DotAt 按局部坐标换算。
    // 页面铺满矩形，圆点条必须由本控件截获，否则点击被子级吞掉。
    return DotAt({p.x - absolute_.x, p.y - absolute_.y}) >= 0;
}

CursorShape Carousel::CursorAt(Point local) const {
    return DotAt(local) >= 0 ? CursorShape::Hand : CursorShape::Arrow;
}

void Carousel::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    const int dot = DotAt(local);
    if (dot != hover_dot_) {
        hover_dot_ = dot;
        Invalidate();
    }
}

void Carousel::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_dot_ != -1) {
        hover_dot_ = -1;
        Invalidate();
    }
}

void Carousel::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    const int index = DotAt(local);
    if (index >= 0) {
        Navigate(static_cast<size_t>(index));
        return;
    }
}

void Carousel::DrawOverlay(Painter& painter, const Theme& theme) {
    if (!dots_ || PageCount() < 2 || absolute_.IsEmpty()) return;
    const Rect strip = DotStrip();
    const float total = static_cast<float>(PageCount() - 1) * 14.0f;
    float x = absolute_.x + strip.x + (strip.w - total) * 0.5f - 3.0f;
    const float y = absolute_.y + strip.y + strip.h * 0.5f;
    for (size_t i = 0; i < PageCount(); ++i) {
        const bool on = i == current_;
        const bool hot = static_cast<int>(i) == hover_dot_ && !on;
        const Rect dot{x, y - (on ? 3.5f : 3.0f), 6.0f, on ? 7.0f : 6.0f};
        painter.FillRoundedRect(
            dot, 3.0f, on ? theme.text : (hot ? theme.text_secondary : theme.text_disabled));
        x += 14.0f;
    }
}

} // namespace lumen
