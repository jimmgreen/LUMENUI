// fluentui/ProgressBar.h — 进度条（确定/不定态）。
#pragma once
#include "Control.h"

namespace fui {

class ProgressBar : public Control {
public:
    float Value() const noexcept { return value_; }
    void SetValue(float value);   // 0..1
    bool Indeterminate() const noexcept { return indeterminate_; }
    void SetIndeterminate(bool value);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();

    float value_ = 0.0f;
    bool indeterminate_ = false;
    float phase_ = 0.0f;   // 不定态动画相位（秒）
};

} // namespace fui
