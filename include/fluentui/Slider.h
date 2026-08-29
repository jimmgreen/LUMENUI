// fluentui/Slider.h — 横向滑块。
#pragma once
#include "Control.h"
#include <functional>

namespace fui {

class Slider : public Control {
public:
    float Min() const noexcept { return min_; }
    float Max() const noexcept { return max_; }
    void SetRange(float min_value, float max_value);
    float Value() const noexcept { return value_; }
    void SetValue(float value);   // 不触发 OnValueChanged
    void OnValueChanged(std::function<void()> handler) { changed_ = std::move(handler); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnFocusChanged(bool focused) override;

    void RelayoutParent();
    void TrackThumb(Point local);

    float min_ = 0.0f;
    float max_ = 100.0f;
    float value_ = 0.0f;
    std::function<void()> changed_;
};

} // namespace fui
