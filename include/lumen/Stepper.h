// lumen/Stepper.h — 步骤条：已完成（勾选）/ 当前 / 待办；点击已完成步骤可回跳。
// Events: OnStepChanged / BindStepChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Animate.h"
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class Stepper : public ControlOf<Stepper> {
public:
    Stepper() = default;

    Stepper& AddStep(std::wstring_view title);
    size_t Count() const noexcept { return titles_.size(); }

    // 0-based；下标 < Current 的步骤为已完成。编程赋值不触发 OnStepChanged。
    Stepper& Current(size_t index);
    size_t Current() const noexcept { return current_; }
    Stepper& OnStepChanged(std::function<void(size_t step)> handler) {
        step_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindStepChanged(std::function<void(size_t step)> handler) {
        return step_changed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;
    bool Focusable() const noexcept override { return !titles_.empty(); }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    CursorShape CursorAt(Point local) const override;

private:
    int StepAt(float x) const;   // -1 无
    void Navigate(size_t step);  // 仅允许回跳（step < current）
    void SnapTo(size_t index);   // 离屏/未入树：动画值直接到位

    std::vector<std::wstring> titles_;
    std::vector<float> step_x_;   // 圆心 x 缓存（Measure 重建，绘制只读）
    std::vector<float> step_w_;
    float text_h_ = 16.0f;        // 标题行盒高（Measure 缓存，绘制期不做文本测量）
    size_t current_ = 0;
    ptrdiff_t hover_ = -1;
    Signal<size_t> step_changed_;
    // 进度头在步进序号空间扫动（决定连接线亮段）；当前圆点按 OutBack 弹出。
    Tween flow_;
    Tween pop_;
    bool pop_ready_ = false;   // Draw 懒初始化：从未配置过时圆点直接到位
};

} // namespace lumen
