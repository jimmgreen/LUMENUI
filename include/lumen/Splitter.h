// lumen/Splitter.h -- draggable seam: hover glow-breathe, drag bloom.
// Events: OnDrag / BindDrag / OnDragEnded / BindDragEnded
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <algorithm>
#include <functional>

namespace lumen {

class Splitter : public ControlOf<Splitter> {
public:
    enum class Orientation { Vertical, Horizontal };
    using Orient = Orientation;

    Splitter() = default;
    explicit Splitter(Orientation orientation) : orientation_(orientation) {}

    Splitter& Orientation(Orient value) {
        orientation_ = value;
        RelayoutParent();
        return *this;
    }
    Orient Orientation() const noexcept { return orientation_; }
    Splitter& Thickness(float value) {
        thickness_ = (std::max)(4.0f, value);
        RelayoutParent();
        return *this;
    }
    float Thickness() const noexcept { return thickness_; }
    bool Dragging() const noexcept { return dragging_; }

    Splitter& OnDrag(std::function<void(float delta)> handler) {
        dragged_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindDrag(std::function<void(float delta)> handler) {
        return dragged_.Connect(std::move(handler));
    }
    Splitter& OnDragEnded(std::function<void()> handler) {
        ended_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindDragEnded(std::function<void()> handler) {
        return ended_.Connect(std::move(handler));
    }

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    CursorShape CursorAt(Point local) const override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnAnimate(float dt_seconds) override;

    float WindowAxis(Point local) const noexcept;

    Orient orientation_ = Orientation::Vertical;
    float thickness_ = 8.0f;
    bool dragging_ = false;
    float last_axis_ = 0.0f;
    float glow_t_ = 0.0f;
    float drag_t_ = 0.0f;
    float breathe_ = 0.0f;
    float breathe_phase_ = 0.0f;
    Signal<float> dragged_;
    Signal<> ended_;
};

} // namespace lumen
