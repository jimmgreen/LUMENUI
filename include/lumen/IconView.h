// lumen/IconView.h — 圆角图标方块（accent 底 + 居中字形）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "InfoBadge.h"
#include <string>

namespace lumen {

class IconView : public ControlOf<IconView> {
public:
    IconView() = default;
    explicit IconView(std::wstring_view glyph) : glyph_(glyph) {}

    IconView& Glyph(std::wstring_view value) { glyph_ = value; Invalidate(); return *this; }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    IconView& Box(float size) { box_ = size; RelayoutParent(); return *this; }
    IconView& Foreground(Color value) { foreground_ = value; Invalidate(); return *this; }
    IconView& Background(Color value) {
        background_ = value;
        custom_background_ = true;
        Invalidate();
        return *this;
    }
    IconView& CornerRadius(float value) { radius_ = value; Invalidate(); return *this; }
    IconView& IconSize(float value) { icon_size_ = value; Invalidate(); return *this; }
    IconView& Weight(float value) { weight_ = value; Invalidate(); return *this; }
    float Weight() const noexcept { return weight_; }
    IconView& Stroke(Color value) { stroke_ = value; custom_stroke_ = true; Invalidate(); return *this; }
    // 虚线描边空心块（Add Component 虚线圆）。
    IconView& Dashed(bool value) { dashed_ = value; Invalidate(); return *this; }
    IconView& Badge(InfoBadgeData data) {
        badge_ = std::move(data);
        Invalidate();
        return *this;
    }
    const InfoBadgeData& Badge() const noexcept { return badge_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }

    void RelayoutParent();

    std::wstring glyph_;
    float box_ = 36.0f;
    float icon_size_ = 16.0f;
    float weight_ = 1.5f;
    float radius_ = 10.0f;
    Color foreground_{0.0f, 0.0f, 0.0f, 0.0f};    // 默认正文色
    Color background_{0.0f, 0.0f, 0.0f, 0.0f};    // 默认 fill_hover
    Color stroke_{0.0f, 0.0f, 0.0f, 0.0f};
    bool custom_background_ = false;
    bool custom_stroke_ = false;
    bool dashed_ = false;
    InfoBadgeData badge_{};
};

} // namespace lumen
