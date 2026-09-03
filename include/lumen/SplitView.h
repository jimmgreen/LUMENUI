// lumen/SplitView.h — 应用骨架：侧边栏 + 主内容区，可折叠导航抽屉。
// Events: OnToggled / BindToggled
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <functional>

namespace lumen {

class Splitter;

class SplitView : public PanelOf<SplitView> {
public:
    // Compact：折叠后保留窄条（放图标）；Hidden：完全收起。
    enum class PaneMode { Compact, Hidden };

    SplitView();

    // 侧边栏 / 主内容容器（纵向）。子控件由各自容器持有。
    StackPanel& Pane();
    StackPanel& Content();
    Splitter& Seam();

    // 展开宽度（默认 220）与折叠宽度（Compact 模式，默认 48）。
    SplitView& PaneLength(float value) {
        pane_length_ = std::max(96.0f, value);
        Place();
        return *this;
    }
    SplitView& CompactLength(float value) {
        compact_length_ = Clamp(value, 0.0f, pane_length_);
        Place();
        return *this;
    }
    // 折叠/展开（宽度缓动过渡）。编程赋值不触发 OnToggled。
    SplitView& Collapse(bool on = true);
    bool Collapsed() const noexcept { return collapsed_; }
    SplitView& Mode(PaneMode mode) {
        mode_ = mode;
        Place();
        return *this;
    }
    SplitView& OnToggled(std::function<void(bool collapsed)> handler) {
        toggled_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindToggled(std::function<void(bool collapsed)> handler) {
        return toggled_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;

private:
    void Place();   // 按当前 pane_w_ 摆放两栏（动画帧内也会调用）
    float PaneWidthTarget() const noexcept;

    StackPanel* pane_ = nullptr;
    StackPanel* content_ = nullptr;
    Splitter* splitter_ = nullptr;
    float pane_length_ = 220.0f;
    float compact_length_ = 48.0f;
    float pane_w_ = 220.0f;   // 当前侧栏宽（动画值）
    bool collapsed_ = false;
    PaneMode mode_ = PaneMode::Compact;
    Signal<bool> toggled_;
};

} // namespace lumen
