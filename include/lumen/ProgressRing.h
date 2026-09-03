// lumen/ProgressRing.h — 环形进度（确定/不定态旋转）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"

namespace lumen {

class ProgressRing : public ControlOf<ProgressRing> {
public:
    float Value() const noexcept { return value_; }
    ProgressRing& Value(float value);   // 0..1
    bool Indeterminate() const noexcept { return indeterminate_; }
    ProgressRing& Indeterminate(bool value);
    ProgressRing& Box(float size) { box_ = size; RelayoutParent(); return *this; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();

    float value_ = 0.0f;
    bool indeterminate_ = false;
    float phase_ = 0.0f;   // 不定态相位（秒）
    float box_ = 40.0f;
};

} // namespace lumen
