// lumen/RangeSlider.h — 双拇指区间滑块；上下界不允许交叉。
// Events: OnValueChanged / BindValueChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>

namespace lumen {

class RangeSlider : public ControlOf<RangeSlider> {
public:
    float Min() const noexcept { return min_; }
    float Max() const noexcept { return max_; }
    RangeSlider& Range(float minimum, float maximum);
    float LowerValue() const noexcept { return lower_; }
    float UpperValue() const noexcept { return upper_; }
    RangeSlider& Values(float lower, float upper);   // 不触发 OnValueChanged
    RangeSlider& Orientation(SliderOrientation value) {
        orientation_ = value;
        RelayoutParent();
        return *this;
    }
    SliderOrientation Orientation() const noexcept { return orientation_; }
    RangeSlider& Step(float value);
    float Step() const noexcept { return step_; }
    RangeSlider& OnValueChanged(std::function<void(float, float)> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindValueChanged(std::function<void(float, float)> handler) {
        return changed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool BlocksCardSpotlight() const noexcept override { return false; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnFocusChanged(bool focused) override;
    bool PrefersDragOverPan() const noexcept override { return true; }

private:
    enum class Thumb { Lower, Upper };
    float Snap(float value) const;
    float Position(float value) const;
    float ValueAt(Point local) const;
    Rect ThumbRect(float value) const;
    void Track(Point local, bool notify);
    float ThumbDip() const noexcept;

    float min_ = 0.0f;
    float max_ = 100.0f;
    float lower_ = 25.0f;
    float upper_ = 75.0f;
    float step_ = 0.0f;
    SliderOrientation orientation_ = SliderOrientation::Horizontal;
    Thumb active_ = Thumb::Lower;
    Signal<float, float> changed_;
};

} // namespace lumen
