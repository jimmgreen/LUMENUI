// lumen/Carousel.h — 轮播：页 = 任意控件，横向滑动切换 + 底部圆点。
// Events: OnPageChanged / BindPageChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <functional>

namespace lumen {

class Carousel : public PanelOf<Carousel> {
public:
    Carousel();

    // 新增一页（任意控件，由 Carousel 持有并铺满）。
    template <typename T, typename... Args>
    T& AddPage(Args&&... args) {
        T& page = Add<T>(std::forward<Args>(args)...);
        Place();
        return page;
    }
    size_t PageCount() const noexcept { return ChildCount(); }

    // 编程切页（横向滑动动画）。不触发 OnPageChanged。
    Carousel& Current(size_t index);
    size_t Current() const noexcept { return current_; }
    // 底部圆点指示器（默认显示，可点击跳页）。
    Carousel& ShowDots(bool value) {
        dots_ = value;
        Invalidate();
        return *this;
    }
    Carousel& OnPageChanged(std::function<void(size_t page)> handler) {
        page_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindPageChanged(std::function<void(size_t page)> handler) {
        return page_changed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size, const Theme&) override;
    void Arrange(const Rect& absolute) override;
    bool ClipChildren() const noexcept override { return true; }
    void DrawOverlay(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;
    bool OnWheel(float delta) override;
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    // 页面子控件铺满矩形，圆点条必须由本控件截获，否则点击被子级吞掉。
    bool CapturesOverlay(Point p) const override;
    CursorShape CursorAt(Point local) const override;

private:
    void Navigate(size_t index);   // 交互路径翻页：触发 OnPageChanged
    void Place();   // 按当前 slide_ 摆放所有页（动画帧内也会调用）
    Rect DotStrip() const noexcept;
    int DotAt(Point local) const;   // -1 无

    float slide_ = 0.0f;   // 当前滑动位置（页单位，缓动值）
    size_t current_ = 0;
    bool dots_ = true;
    int hover_dot_ = -1;
    Signal<size_t> page_changed_;
};

} // namespace lumen
