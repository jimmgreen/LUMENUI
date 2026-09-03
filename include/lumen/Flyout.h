// lumen/Flyout.h — 轻量弹层：锚定控件的任意内容浮层，点窗外轻触关闭。
// Events: OnClosed / BindClosed
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <algorithm>
#include <functional>

namespace lumen {

// 弹出方位。空间不足时窗口层自动翻转到另一侧并钳进客户区。
enum class FlyoutPlacement { Below, Above };

class Flyout : public StackPanel {
public:
    Flyout();
    ~Flyout() override;

    Flyout& Placement(FlyoutPlacement value) {
        placement_ = value;
        return *this;
    }
    FlyoutPlacement Placement() const noexcept { return placement_; }
    FlyoutPlacement PlacementWay() const noexcept { return Placement(); }
    // 弹层宽度（内容纵向自适应高度）。默认 260 DIP。
    Flyout& FlyoutWidth(float value) {
        width_ = std::max(120.0f, value);
        return *this;
    }
    float FlyoutWidth() const noexcept { return width_; }
    Flyout& OnClosed(std::function<void()> handler) {
        closed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClosed(std::function<void()> handler) {
        return closed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    // 卡片底 fill_input 是半透明 token；浮层必须先垫不透明背景色再画卡片。
    void Draw(Painter& painter, const Theme& theme) override;
    FlyoutPlacement placement_ = FlyoutPlacement::Below;
    float width_ = 260.0f;
    Signal<> closed_;
};

} // namespace lumen
