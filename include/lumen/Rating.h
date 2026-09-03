// lumen/Rating.h — 评分：单色星形行，支持部分填充与悬停预览。
// Events: OnRated / BindRated
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <algorithm>
#include <functional>

namespace lumen {

class Rating : public ControlOf<Rating> {
public:
    Rating() = default;
    explicit Rating(int max_stars) : max_(max_stars < 1 ? 5 : max_stars) {}

    Rating& Max(int value) {
        max_ = value < 1 ? 1 : (value > 10 ? 10 : value);
        value_ = std::min(value_, static_cast<double>(max_));
        RelayoutParent();
        return *this;
    }
    int Max() const noexcept { return max_; }
    // 0..Max，支持小数部分填充。编程赋值不触发 OnRated。
    Rating& Value(double value);
    double Value() const noexcept { return value_; }
    Rating& ReadOnly(bool value) {
        read_only_ = value;
        Invalidate();
        return *this;
    }
    bool ReadOnly() const noexcept { return read_only_; }
    // 星形边长（行高 = 星形尺寸）。默认 20。
    Rating& StarSize(float value) {
        star_size_ = std::max(12.0f, value);
        RelayoutParent();
        return *this;
    }
    Rating& OnRated(std::function<void(int)> handler) {
        rated_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindRated(std::function<void(int)> handler) {
        return rated_.Connect(std::move(handler));
    }
    Rating& BindValue(Property<int>& p);

protected:
    friend class WindowImpl;
    Size Measure(Size, const Theme&) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return !read_only_; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;

private:
    int StarAt(Point local) const;   // -1 = 星形区外
    Rect StarRect(int index) const noexcept;

    int max_ = 5;
    double value_ = 0.0;
    int hover_star_ = -1;
    float star_size_ = 20.0f;
    bool read_only_ = false;
    Signal<int> rated_;
    ScopedConnection value_prop_;
    ScopedConnection value_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
