// lumen/Expander.h — 手风琴：标题 + 箭头，无齿轮；聚光需显式 Spotlight(true)。
// Events: OnExpandedChanged / BindExpandedChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include "Animate.h"
#include <functional>
#include <string>

namespace lumen {

class Expander : public PanelOf<Expander> {
public:
    Expander() = default;
    explicit Expander(std::wstring_view title) : title_(title) {}

    Expander& Title(std::wstring_view value) { title_ = value; RelayoutParent(); return *this; }
    const std::wstring& Title() const noexcept { return title_; }
    bool Expanded() const noexcept { return expanded_; }
    Expander& Expanded(bool value);
    Expander& OnExpandedChanged(std::function<void()> handler) { changed_.Subscribe(std::move(handler)); return *this; }
    Connection BindExpandedChanged(std::function<void()> handler) {
        return changed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Group;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternExpand; }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? title_ : accessible_name_;
    }
    int AutomationExpandState() const noexcept override { return expanded_ ? 1 : 0; }
    bool AutomationExpand() override {
        if (!expanded_) Expanded(true);
        return true;
    }
    bool AutomationCollapse() override {
        if (expanded_) Expanded(false);
        return true;
    }
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;
    bool ClipChildren() const noexcept override { return true; }

    void RelayoutParent();

    std::wstring title_;
    bool expanded_ = true;
    float chevron_t_ = 1.0f;   // 箭头翻转 0..1（收起→展开）
    float open_t_ = 1.0f;      // 内容区高度 0..1，收起时裁切子级
    Tween open_tween_{};
    Signal<> changed_;
};

} // namespace lumen
