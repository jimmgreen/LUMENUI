// lumen/Drawer.h — 贴边全高临时抽屉，由 Window::ShowDrawer 弹出。
// Events: OnClosed / BindClosed
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Animate.h"
#include "Panel.h"
#include "Signal.h"
#include <algorithm>
#include <functional>

namespace lumen {

enum class Edge { Left, Right };

class Drawer : public StackPanel {
public:
    Drawer();
    ~Drawer() override;

    Drawer& PanelWidth(float dip) {
        panel_w_ = std::max(160.0f, dip);
        return *this;
    }
    float PanelWidth() const noexcept { return panel_w_; }
    Edge Side() const noexcept { return edge_; }

    Drawer& OnClosed(std::function<void()> handler) {
        closed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClosed(std::function<void()> handler) {
        return closed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;
    bool OnKey(uint32_t vk) override;
    bool ClipChildren() const noexcept override { return true; }

    void BeginOpen(Edge edge);
    void BeginClose();

    Edge edge_ = Edge::Right;
    float panel_w_ = 320.0f;
    Tween slide_{};
    bool closing_ = false;
    Signal<> closed_;
};

} // namespace lumen
