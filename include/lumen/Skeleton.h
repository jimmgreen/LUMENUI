// lumen/Skeleton.h — 加载占位骨架：亮度阶梯圆角条 + 呼吸微光（仅 Active 期间推进相位）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"

namespace lumen {

class Skeleton : public ControlOf<Skeleton> {
public:
    Skeleton() = default;
    // 单行骨架；宽高可用 SetBounds/Grow 指定，默认 160×12。
    explicit Skeleton(float width_dip, float height_dip = 12.0f)
        : custom_width_(width_dip), custom_height_(height_dip) {}

    // 多行文本占位：行高 = 高度/行数，末行缩短 40%。
    Skeleton& Lines(int count) {
        lines_ = count < 1 ? 1 : count;
        RelayoutParent();
        return *this;
    }
    int Lines() const noexcept { return lines_; }
    Skeleton& Round(bool value) {
        round_ = value;
        Invalidate();
        return *this;
    }
    // 呼吸微光开关。Active(true) 是显式播放期，动画时钟在其间持续运转。
    Skeleton& Active(bool value) {
        if (active_ == value) return *this;
        active_ = value;
        if (active_) Animate();
        Invalidate();
        return *this;
    }
    bool Active() const noexcept { return active_; }

protected:
    friend class WindowImpl;
    Size Measure(Size, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;
    // 骨架是占位装饰，不拦截鼠标。
    bool HitTransparent() const noexcept override { return true; }

private:
    float custom_width_ = 0.0f;
    float custom_height_ = 0.0f;
    int lines_ = 1;
    bool round_ = true;
    bool active_ = true;
    float phase_ = 0.0f;
};

} // namespace lumen
