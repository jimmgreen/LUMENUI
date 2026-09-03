// lumen/TeachingTip.h — 带箭头的引导气泡：锚定控件，点窗外 / Esc / × 关闭。
// Events: OnClosed / BindClosed
// Keys: 可聚焦，参与 Tab 环
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <algorithm>
#include <functional>
#include <string>

namespace lumen {

class Button;

enum class TeachingTipPlacement { Auto, Below, Above };

class TeachingTip : public PanelOf<TeachingTip> {
public:
    TeachingTip();
    ~TeachingTip() override;

    TeachingTip& Title(std::wstring_view value) {
        title_ = value;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Title() const noexcept { return title_; }
    TeachingTip& Message(std::wstring_view value) {
        message_ = value;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Message() const noexcept { return message_; }
    TeachingTip& Glyph(std::wstring_view value) {
        glyph_ = value;
        RelayoutParent();
        return *this;
    }
    TeachingTip& Placement(TeachingTipPlacement value) {
        placement_ = value;
        return *this;
    }
    TeachingTipPlacement Placement() const noexcept { return placement_; }
    TeachingTipPlacement PlacementWay() const noexcept { return Placement(); }
    TeachingTip& TipWidth(float value) {
        width_ = std::max(160.0f, value);
        RelayoutParent();
        return *this;
    }
    float TipWidth() const noexcept { return width_; }
    float TailLength() const noexcept { return 8.0f; }
    TeachingTip& Closable(bool value) {
        closable_ = value;
        RelayoutParent();
        return *this;
    }
    TeachingTip& Action(std::wstring_view label, std::function<void()> on_click);
    TeachingTip& OnClosed(std::function<void()> handler) {
        closed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClosed(std::function<void()> handler) {
        return closed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool ClipChildren() const noexcept override { return true; }
    bool Focusable() const noexcept override { return true; }
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;

    void RelayoutParent();
    void Dismiss();
    void SetTailTarget(Point window_dip, bool pointing_up);
    Rect CloseRect() const noexcept;

    std::wstring title_;
    std::wstring message_;
    std::wstring glyph_;
    TeachingTipPlacement placement_ = TeachingTipPlacement::Auto;
    float width_ = 280.0f;
    bool closable_ = true;
    bool close_hot_ = false;
    bool close_press_ = false;
    Point tail_target_{};
    bool tail_up_ = true;
    Button* action_ = nullptr;
    std::function<void()> action_cb_;
    Signal<> closed_;
};

} // namespace lumen
