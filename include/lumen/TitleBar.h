// lumen/TitleBar.h -- client-frame caption: icon, title, content slot, min/max/close.
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace lumen {

class TitleBar : public PanelOf<TitleBar> {
public:
    enum class Region { Client, Caption, Min, Max, Close };

    TitleBar();
    ~TitleBar() override;

    static constexpr float kHeight = 40.0f;
    static constexpr float kButtonWidth = 46.0f;

    float Height() const noexcept { return kHeight; }

    TitleBar& Title(std::wstring_view text) {
        title_ = text;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Title() const noexcept { return title_; }
    TitleBar& Glyph(std::wstring_view glyph) {
        glyph_ = glyph;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    // ico/png/jpeg 等 WIC 可解码图；标题栏左侧画位图，优先于 Glyph。
    bool LoadIconMemory(std::span<const std::byte> encoded);
    TitleBar& ClearIcon();
    bool HasIcon() const noexcept;
    TitleBar& Status(std::wstring_view text) {
        if (status_ == text) return *this;
        status_ = text;
        Invalidate();
        return *this;
    }
    const std::wstring& Status() const noexcept { return status_; }

    // Interactive chrome in the middle (HitTransparent row so empty area stays Caption).
    StackPanel& Content() { return *content_; }

    Region Hit(Point window_dip) const noexcept;
    void SetButtonHover(Region region);
    void Maximized(bool value);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Header;
    }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? title_ : accessible_name_;
    }
    void Arrange(const Rect& absolute) override;
    void Prepare(Painter& painter) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnAnimate(float dt_seconds) override;
    bool HitTransparent() const noexcept override { return true; }

    Rect ButtonSlot(int index) const noexcept;
    int HoverIndex() const noexcept;
    float CaptionStart() const noexcept;
    float TitleWidth(float bar_w) const;

    StackPanel* content_ = nullptr;
    struct IconImage;
    std::unique_ptr<IconImage> icon_image_;
    std::wstring title_;
    std::wstring glyph_;
    std::wstring status_;
    Region hover_ = Region::Caption;
    bool maximized_ = false;
    float min_glow_ = 0.0f;
    float max_glow_ = 0.0f;
    float close_glow_ = 0.0f;
};

} // namespace lumen
