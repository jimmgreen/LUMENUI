// lumen/Gauge.h — 240° 径向仪表：刻度 + 弧段，超阈全亮。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"

namespace lumen {

class Gauge : public ControlOf<Gauge> {
public:
    Gauge& Range(float min_value, float max_value);
    Gauge& Value(float value);   // 编程赋值不触发回调
    float Value() const noexcept { return value_; }
    Gauge& Threshold(float value) {
        threshold_ = value;
        Invalidate();
        return *this;
    }
    float Threshold() const noexcept { return threshold_; }
    Gauge& Caption(std::wstring_view text) {
        caption_ = std::wstring(text);
        RelayoutParent();
        return *this;
    }
    const std::wstring& Caption() const noexcept { return caption_; }
    Gauge& Unit(std::wstring_view text) {
        unit_ = std::wstring(text);
        Invalidate();
        return *this;
    }
    const std::wstring& Unit() const noexcept { return unit_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;

    void RelayoutParent();

    float min_ = 0.0f;
    float max_ = 100.0f;
    float value_ = 0.0f;
    float threshold_ = 1.0e9f;
    std::wstring caption_;
    std::wstring unit_;
};

} // namespace lumen
