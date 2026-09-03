#include "lumen/Rating.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>

namespace lumen {
namespace {
constexpr float kGap = 4.0f;
} // namespace

Rating& Rating::Value(double value) {
    value = Clamp(value, 0.0, static_cast<double>(max_));
    if (value_ == value) return *this;
    value_ = value;
    Invalidate();
    return *this;
}

Rect Rating::StarRect(int index) const noexcept {
    const float step = star_size_ + kGap;
    return {absolute_.x + static_cast<float>(index) * step, absolute_.y, star_size_,
            star_size_};
}

Size Rating::Measure(Size, const Theme&) {
    const float step = star_size_ + kGap;
    return {star_size_ + step * static_cast<float>(max_ - 1), star_size_};
}

int Rating::StarAt(Point local) const {
    const float step = star_size_ + kGap;
    if (local.y < 0.0f || local.y > star_size_ || local.x < 0.0f) return -1;
    const int index = static_cast<int>(local.x / step);
    if (index < 0 || index >= max_) return -1;
    return index;
}

bool Rating::OnKey(uint32_t vk) {
    if (read_only_) return false;
    switch (vk) {
    case VK_LEFT:
    case VK_DOWN:
        Value(value_ - 1.0);
        rated_.Emit(static_cast<int>(value_));
        return true;
    case VK_RIGHT:
    case VK_UP:
        Value(value_ + 1.0);
        rated_.Emit(static_cast<int>(value_));
        return true;
    case VK_HOME:
        Value(0.0);
        rated_.Emit(0);
        return true;
    case VK_END:
        Value(static_cast<double>(max_));
        rated_.Emit(max_);
        return true;
    default:
        return false;
    }
}

void Rating::OnMouseDown(Point local, uint32_t buttons) {
    if (read_only_ || !(buttons & 0x0001)) return;
    Focus();
    const int star = StarAt(local);
    if (star < 0) return;
    Value(star < static_cast<int>(value_) && star + 1 == static_cast<int>(value_)
              ? static_cast<double>(star)   // 点当前值同星 = 取消一档
              : static_cast<double>(star + 1));
    rated_.Emit(static_cast<int>(value_));
}

void Rating::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    const int star = read_only_ ? -1 : StarAt(local);
    if (star != hover_star_) {
        hover_star_ = star;
        Invalidate();
    }
}

void Rating::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_star_ != -1) {
        hover_star_ = -1;
        Invalidate();
    }
}

void Rating::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

void Rating::Draw(Painter& painter, const Theme& theme) {
    const double shown = enabled_ && hover_star_ >= 0 ? static_cast<double>(hover_star_ + 1)
                                                      : value_;
    for (int i = 0; i < max_; ++i) {
        const Rect rect = StarRect(i);
        // 底星：暗阶；填充星：亮阶。部分填充用裁切到小数宽度实现。
        painter.DrawIcon(icon::kFavorite, rect, star_size_ - 2.0f, theme.text_disabled);
        const double fill = Clamp(shown - static_cast<double>(i), 0.0, 1.0);
        if (fill <= 0.0) continue;
        painter.PushClip({rect.x, rect.y, rect.w * static_cast<float>(fill), rect.h});
        painter.DrawIcon(icon::kFavoriteFill, rect, star_size_ - 2.0f, theme.text);
        painter.PopClip();
    }
    if (focused_ && enabled_ && !read_only_) {
        const Size desired = Measure({}, theme);
        PaintFocusRing(painter, theme, {absolute_.x, absolute_.y, desired.w, desired.h},
                       theme.radius_control);
    }
}

Rating& Rating::BindValue(Property<int>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Value(static_cast<double>(p.Get()));
        bind_loop_ = false;
    };
    apply();
    value_prop_ = ScopedConnection(p.OnChanged([apply](const int&) { apply(); }));
    value_ctrl_ = ScopedConnection(rated_.Connect([this, &p](int v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = v;
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
