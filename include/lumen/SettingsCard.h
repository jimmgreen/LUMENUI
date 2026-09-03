// lumen/SettingsCard.h — 提示/设置行：左侧光条 + 图标 + 标题/描述 + 尾部控件。
// Events: OnClick / BindClick
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class SettingsCard : public PanelOf<SettingsCard> {
public:
    SettingsCard() = default;   // 聚光需显式 Spotlight(true)（追光只属于底部聚光卡）

    SettingsCard& Title(std::wstring_view value) { title_ = value; RelayoutParent(); return *this; }
    const std::wstring& Title() const noexcept { return title_; }
    SettingsCard& Description(std::wstring_view value) { description_ = value; RelayoutParent(); return *this; }
    const std::wstring& Description() const noexcept { return description_; }
    // 左侧 Segoe Fluent Icons 字形（可空）。
    SettingsCard& Glyph(std::wstring_view value) { glyph_ = value; Invalidate(); return *this; }
    // 左图标底色块（默认无底）。
    SettingsCard& GlyphBackground(Color value) { glyph_bg_ = value; Invalidate(); return *this; }
    // 整卡点击（Compound Action 类操作卡）；尾部控件区域点击不触发。
    SettingsCard& OnClick(std::function<void()> handler) {
        click_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClick(std::function<void()> handler) { return click_.Connect(std::move(handler)); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Group;
    }
    uint32_t AutomationPatterns() const noexcept override {
        return click_.Empty() ? 0u : kPatternInvoke;
    }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? title_ : accessible_name_;
    }
    bool AutomationInvoke() override {
        if (!enabled_ || click_.Empty()) return false;
        click_.Emit();
        return true;
    }
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool Focusable() const noexcept override { return !click_.Empty(); }
    bool OnKey(uint32_t vk) override;
    void OnFocusChanged(bool focused) override;

    void RelayoutParent();

    std::wstring title_;
    std::wstring description_;
    std::wstring glyph_;
    Color glyph_bg_{0.0f, 0.0f, 0.0f, 0.0f};
    float text_left_ = 0.0f;    // Arrange 阶段计算
    float text_width_ = 0.0f;
    Signal<> click_;
};

} // namespace lumen
