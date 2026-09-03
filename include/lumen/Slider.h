// lumen/Slider.h — 支持横向与竖向的单值滑块。
// Events: OnValueChanged / BindValueChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <algorithm>
#include <functional>

namespace lumen {

class Slider : public ControlOf<Slider> {
public:
    float Min() const noexcept { return min_; }
    float Max() const noexcept { return max_; }
    Slider& Range(float min_value, float max_value);
    float Value() const noexcept { return value_; }
    Slider& Value(float value);   // 不触发 OnValueChanged
    Slider& Orientation(SliderOrientation value) { orientation_ = value; RelayoutParent(); return *this; }
    SliderOrientation Orientation() const noexcept { return orientation_; }
    Slider& Step(float value) { step_ = std::max(0.0f, value); return *this; }
    float Step() const noexcept { return step_; }
    Slider& OnValueChanged(std::function<void(float)> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindValueChanged(std::function<void(float)> handler) {
        return changed_.Connect(std::move(handler));
    }
    Slider& BindValue(Property<float>& p, float scale = 1.0f);
    Slider& BindValue(Property<double>& p, double scale = 1.0);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Slider;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternRange; }
    double AutomationRangeValue() const override { return value_; }
    double AutomationRangeMin() const noexcept override { return min_; }
    double AutomationRangeMax() const noexcept override { return max_; }
    double AutomationRangeSmall() const noexcept override {
        return step_ > 0.0f ? static_cast<double>(step_) : 1.0;
    }
    bool AutomationSetRange(double value) override {
        if (!enabled_) return false;
        Value(static_cast<float>(value));
        return true;
    }
    bool AutomationIsReadOnly() const noexcept override { return false; }
    bool BlocksCardSpotlight() const noexcept override { return false; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnFocusChanged(bool focused) override;
    bool PrefersDragOverPan() const noexcept override { return true; }

    void RelayoutParent();
    void TrackThumb(Point local);
    float KnobDip() const noexcept;

    float min_ = 0.0f;
    float max_ = 100.0f;
    float value_ = 0.0f;
    float step_ = 0.0f;
    SliderOrientation orientation_ = SliderOrientation::Horizontal;
    Signal<float> changed_;
    ScopedConnection value_prop_;
    ScopedConnection value_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
