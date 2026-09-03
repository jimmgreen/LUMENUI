// lumen/ProgressBar.h — 进度条（确定/不定态）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"

namespace lumen {

class ProgressBar : public ControlOf<ProgressBar> {
public:
    float Value() const noexcept { return value_; }
    ProgressBar& Value(float value);   // 0..1
    bool Indeterminate() const noexcept { return indeterminate_; }
    ProgressBar& Indeterminate(bool value);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::ProgressBar;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternRange; }
    double AutomationRangeValue() const override { return value_; }
    double AutomationRangeMin() const noexcept override { return 0.0; }
    double AutomationRangeMax() const noexcept override { return 1.0; }
    bool AutomationIsReadOnly() const noexcept override { return true; }
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();

    float value_ = 0.0f;
    bool indeterminate_ = false;
    float phase_ = 0.0f;   // 不定态动画相位（秒）
};

} // namespace lumen
