// lumen/Separator.h — 1px 分隔线，交叉轴拉满。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"

namespace lumen {

class Separator : public ControlOf<Separator> {

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Separator;
    }
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }
};

} // namespace lumen
