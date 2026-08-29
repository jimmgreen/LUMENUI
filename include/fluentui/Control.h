// fluentui/Control.h — 控件基类。状态与动画归控件所有，窗口负责输入路由与重绘调度。
#pragma once
#include "Theme.h"
#include <cstdint>
#include <functional>

namespace fui {

class Painter;
class Window;
class Panel;


class Control {
public:
    Control() = default;
    virtual ~Control();
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    bool Visible() const noexcept { return visible_; }
    void SetVisible(bool value);
    bool Enabled() const noexcept { return enabled_; }
    void SetEnabled(bool value);

    // 相对父容器的位置（DIP）。布局容器（StackPanel 等）会接管此值。
    const Rect& Bounds() const noexcept { return bounds_; }
    void SetBounds(const Rect& r);
    // 最近一次布局后的窗口客户区绝对矩形（DIP）。
    const Rect& AbsoluteBounds() const noexcept { return absolute_; }
    Size DesiredSize() const noexcept { return desired_; }

protected:
    friend class Window;
    friend class Panel;
    friend void DrawControlTree(Painter& painter, const Theme& theme, Control* root);

    // 布局：Measure 计算期望尺寸，Arrange 得到绝对矩形并排布子级。
    virtual Size Measure(Size available, const Theme& theme);
    virtual void Arrange(const Rect& absolute);
    virtual void Draw(Painter& painter, const Theme& theme) = 0;

    // 输入（局部坐标，DIP）。返回 true 表示事件已处理。
    virtual void OnMouseEnter();
    virtual void OnMouseLeave();
    virtual void OnMouseMove(Point local, uint32_t buttons) { (void)local; (void)buttons; }
    virtual void OnMouseDown(Point local, uint32_t buttons) { (void)local; (void)buttons; }
    virtual void OnMouseUp(Point local, uint32_t buttons) { (void)local; (void)buttons; }
    virtual void OnMouseDoubleClick(Point local) { (void)local; }
    virtual void OnWheel(float delta) { (void)delta; }
    virtual bool OnKey(uint32_t vk) { (void)vk; return false; }
    virtual bool OnChar(wchar_t ch) { (void)ch; return false; }
    virtual void OnFocusChanged(bool focused) { (void)focused; }
    virtual bool Focusable() const noexcept { return false; }
    // 命中测试穿透（如静态文本）：鼠标事件落到其下的控件。
    virtual bool HitTransparent() const noexcept { return false; }

    // 动画时钟：返回 true 表示仍需继续每帧回调。
    // Fluent 的状态切换是瞬时变色；仅个别动画（开关滑块、进度不定态、
    // 滚动条展开、平滑滚动）使用此时钟。
    virtual bool OnAnimate(float dt_seconds);

    void Invalidate();            // 请求重绘
    void Animate();               // 请求动画时钟（空闲时定时器自动停止）
    void RelayoutParent();        // 影响布局的属性变化后调用
    bool HasFocus() const noexcept { return focused_; }
    void SetFocus();
    void* NativeWindow() const;   // HWND，用于剪贴板/弹窗定位

    // 0..1 指数平滑。到位返回 false。（仅用于动画型控件，状态切换不走此路径）
    static bool EaseTo(float& value, float target, float dt, float speed = 12.0f,
                       float epsilon = 0.002f);

    // 输入状态（瞬时生效，无过渡）
    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;

    Rect bounds_;        // 相对父级
    Rect absolute_;      // Arrange 阶段算出
    Size desired_;
    bool visible_ = true;
    bool enabled_ = true;
    Panel* parent_ = nullptr;
    Window* window_ = nullptr;   // 所在窗口（根控件由窗口注入，随容器下传）

private:
    friend class WindowImpl;
};

} // namespace fui
