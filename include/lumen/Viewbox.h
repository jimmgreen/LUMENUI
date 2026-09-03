// lumen/Viewbox.h — 子级按自然尺寸排布，再等比（或拉伸）缩放到容器。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"

namespace lumen {

enum class ViewboxStretch { Uniform, UniformToFill, Fill, None };

// 只缩放第一个可见子级；多个请先包进 Column / Grid。
// 排布仍用自然尺寸（否则会重排/换行）；缩放只发生在绘制与指针映射。
class Viewbox : public PanelOf<Viewbox> {
public:
    Viewbox() = default;

    Viewbox& Stretch(ViewboxStretch value);
    ViewboxStretch Stretch() const noexcept { return stretch_; }
    Viewbox& HorizontalAlignment(Align value);
    Viewbox& VerticalAlignment(Align value);

protected:
    friend class WindowImpl;
    Point MapToChildren(Point window_dip) const override;
    Rect MapClipToChildren(const Rect& clip) const override;
    void PushChildDraw(Painter& painter) const override;
    void PopChildDraw(Painter& painter) const override;
    // 视口裁切走 Layer（PushChildDraw），不走轴对齐 ClipChildren。
    bool ClipChildren() const noexcept override { return false; }
    void Prepare(Painter& painter) override;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;

    size_t ContentIndex() const;
    void ComputeScale(Size natural, Size viewport, float& sx, float& sy) const;

    ViewboxStretch stretch_ = ViewboxStretch::Uniform;
    Align align_x_ = Align::Center;
    Align align_y_ = Align::Center;
    Point origin_{};
    float sx_ = 1.0f;
    float sy_ = 1.0f;
};

} // namespace lumen
