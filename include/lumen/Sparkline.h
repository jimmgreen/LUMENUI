// lumen/Sparkline.h — 迷你趋势图：折线 / 柱状 / 面积，回调或 span 数据。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <span>

namespace lumen {

enum class SparklineStyle { Line, Bar, Area };

class Sparkline : public ControlOf<Sparkline> {
public:
    static constexpr size_t kMaxSamples = 256;

    Sparkline& Count(size_t count) {
        count_ = count;
        Invalidate();
        return *this;
    }
    size_t Count() const noexcept { return count_; }
    Sparkline& Values(std::function<float(size_t)> provider) {
        values_ = std::move(provider);
        use_store_ = false;
        Invalidate();
        return *this;
    }
    Sparkline& Values(std::span<const float> values);
    Sparkline& Values(std::initializer_list<float> values) {
        return Values(std::span<const float>(values.begin(), values.size()));
    }
    Sparkline& Style(SparklineStyle value) {
        style_ = value;
        Invalidate();
        return *this;
    }
    SparklineStyle Style() const noexcept { return style_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool HitTransparent() const noexcept override { return false; }

    size_t count_ = 0;
    size_t stored_ = 0;
    bool use_store_ = false;
    SparklineStyle style_ = SparklineStyle::Line;
    std::function<float(size_t)> values_;
    float stored_values_[kMaxSamples]{};
};

} // namespace lumen
