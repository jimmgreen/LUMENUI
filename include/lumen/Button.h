// lumen/Button.h — 按钮。
// Events: OnClick / BindClick
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Command.h"
#include "Signal.h"
#include <functional>
#include <memory>
#include <string>

namespace lumen {

class ToolTip;

// Primary = 白底黑字 + 外发光，悬停增辉；Standard = 黑底 20% 描边，悬停描边转白+外发光；
// Subtle/Transparent = 幽灵；Danger = 白热警示。Shimmer(true) 叠加锥形流光边框。
// 悬停不做位移（发光体不是实体键），按压中心轻微收缩。
enum class ButtonKind { Standard, Primary, Subtle, Transparent, Danger };

enum class ButtonSize { Small, Medium, Large };

class Button : public ControlOf<Button> {
public:
    Button() = default;
    explicit Button(std::wstring_view text, ButtonKind kind = ButtonKind::Standard)
        : text_(text), kind_(kind) {}
    Button(std::wstring_view text, ButtonKind kind, std::function<void()> on_click)
        : text_(text), kind_(kind) {
        click_.Subscribe(std::move(on_click));
    }
    Button(Button&& other) noexcept;
    Button& operator=(Button&& other) noexcept;

    const std::wstring& Text() const noexcept { return text_; }
    Button& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    Button& Text(std::string_view utf8) { return Text(U8(utf8)); }
    // Segoe Fluent Icons 字形（见 lumen::icon）。
    const std::wstring& Glyph() const noexcept { return glyph_; }
    Button& Glyph(std::wstring_view value) { glyph_ = value; RelayoutParent(); return *this; }
    ButtonKind Kind() const noexcept { return kind_; }
    Button& Kind(ButtonKind value) { kind_ = value; Invalidate(); return *this; }
    // 尺寸变体：Small 40（幽灵图标/行内操作）/ Medium·Large 44（px-6 py-2.5 + 16px 字）。
    ButtonSize SizeClass() const noexcept { return size_; }
    Button& SizeClass(ButtonSize value) { size_ = value; RelayoutParent(); return *this; }
    // 实例级高度覆盖（DIP）：0 = 跟随尺寸档。用于个别紧凑位置，不动全局规格。
    Button& Height(float value) { height_override_ = value; RelayoutParent(); return *this; }
    // 胶囊形：圆角取高度的一半。
    Button& Pill(bool value) { pill_ = value; Invalidate(); return *this; }
    bool Pill() const noexcept { return pill_; }
    // 流光边框：静止时半透明锥形环，悬停/聚焦期间旋转（4s 一圈）。
    Button& Shimmer(bool value) { shimmer_ = value; Invalidate(); return *this; }
    bool Shimmer() const noexcept { return shimmer_; }

    Button& OnClick(std::function<void()> handler) {
        click_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClick(std::function<void()> handler) { return click_.Connect(std::move(handler)); }
    Button& Bind(Command& command);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    float ChromeRadius(const Theme& theme) const noexcept override {
        return pill_ ? absolute_.h * 0.5f : theme.radius_control;
    }
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Button;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternInvoke; }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    bool AutomationInvoke() override {
        if (!enabled_) return false;
        click_.Emit();
        if (command_) command_->Execute();
        return true;
    }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();

    std::wstring text_;
    std::wstring glyph_;
    ButtonKind kind_ = ButtonKind::Standard;
    ButtonSize size_ = ButtonSize::Medium;
    float height_override_ = 0.0f;
    bool pill_ = false;
    bool shimmer_ = false;
    float glow_t_ = 0.0f;        // 悬停辉光 0..1
    float scale_t_ = 0.0f;       // 按压缩放 0..1
    float shimmer_angle_ = 0.0f; // 流光相位（弧度）
    float bloom_t_ = 0.0f;       // 按压光爆 1→0
    Point bloom_at_{};
    Signal<> click_;
    Command* command_ = nullptr;
    ScopedConnection cmd_changed_;
    ScopedConnection cmd_destroyed_;

    void RebindCommand();
};

} // namespace lumen
