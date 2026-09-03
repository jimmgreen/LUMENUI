// lumen/ScrollViewer.h — 裁切视口 + 覆盖滚动条。默认纵向；内容通常是一个 Column。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Animate.h"
#include "Panel.h"

namespace lumen {

// ScrollIntoView 对齐：Nearest 只在完全看不见时滚；其余把目标贴到视口边/中。
enum class ScrollAlignment { Nearest, Start, Center, End };

class ScrollViewer : public PanelOf<ScrollViewer> {
public:
    ScrollViewer& Vertical(bool enabled) {
        vertical_ = enabled;
        Relayout();
        return *this;
    }
    ScrollViewer& Horizontal(bool enabled) {
        horizontal_ = enabled;
        Relayout();
        return *this;
    }

    float OffsetX() const noexcept { return scroll_x_; }
    float OffsetY() const noexcept { return scroll_y_; }
    float ContentWidth() const noexcept { return content_w_; }
    float ContentHeight() const noexcept { return content_h_; }
    // animate=false 或离屏时瞬时到位；有窗口时默认弹簧趋近。
    ScrollViewer& ScrollToX(float value, bool animate = true);
    ScrollViewer& ScrollToY(float value, bool animate = true);
    // 把绝对坐标矩形滚进视口（焦点滚入视野）。
    void EnsureVisible(const Rect& absolute_child);
    void ScrollIntoView(Control& child, ScrollAlignment align = ScrollAlignment::Nearest);

    // 内容高度变化时钉住视口里的锚点（默认开：上方插入不把当前行顶走）。
    ScrollViewer& AnchorEnabled(bool on = true);
    bool AnchorEnabled() const noexcept { return anchor_enabled_; }
    // 0 = 视口顶/左，1 = 底/右。未指定 Anchor 控件时按此点选取子孙。
    ScrollViewer& AnchorRatio(float y);
    float AnchorRatio() const noexcept { return anchor_ratio_y_; }
    ScrollViewer& Anchor(Control* child);
    Control* Anchor() const noexcept { return anchor_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void DrawOverlay(Painter& painter, const Theme& theme) override;
    bool ClipChildren() const noexcept override { return true; }
    bool CapturesOverlay(Point p) const override;
    bool CanPan() const noexcept override;
    void PanBy(float dx, float dy) override;
    void PanFling(float vx, float vy) override;
    bool PrefersDragOverPan() const noexcept override { return drag_axis_ != 0; }
    bool OnWheel(float delta) override;
    bool OnHWheel(float delta) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnAnimate(float dt_seconds) override;

    float MaxScrollX() const noexcept;
    float MaxScrollY() const noexcept;
    void PlaceContent();
    void ClampOffsets();
    void SnapToTarget();
    bool OwnsDescendant(const Control& node) const;
    Control* DeepestAt(Control& node, Point world) const;
    Control* ResolveAnchor();
    void ApplyAnchor(Control* keep, float rel_x, float rel_y);
    void BringIntoView(const Rect& absolute_child, ScrollAlignment align);
    void ScrollAxisTo(float& target, float& scroll, SpringMotion& spring, float value,
                      bool animate);
    Rect VerticalTrack() const noexcept;
    Rect HorizontalTrack() const noexcept;
    Color ThumbColor(const Theme& theme) const noexcept;

    bool vertical_ = true;
    bool horizontal_ = false;
    bool anchor_enabled_ = true;
    float content_w_ = 0.0f;
    float content_h_ = 0.0f;
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    float target_x_ = 0.0f;
    float target_y_ = 0.0f;
    float expand_t_ = 0.0f;
    float wheel_t_ = 0.0f;
    float hide_idle_ = 0.0f;
    float thumb_alpha_ = 1.0f;
    float anchor_ratio_y_ = 0.0f;
    SpringMotion spring_x_{};
    SpringMotion spring_y_{};
    Control* anchor_ = nullptr;
    int drag_axis_ = 0;   // 0 无，1 纵向，2 横向
    float drag_grab_ = 0.0f;
    bool panning_content_ = false;
};

} // namespace lumen
