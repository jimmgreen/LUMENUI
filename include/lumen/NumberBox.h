// lumen/NumberBox.h — 数字输入框：字符过滤、↑↓ 步进、失焦钳制格式化、右侧 spin 区。
// Events: OnValueChanged / BindValueChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "TextBox.h"
#include "Animate.h"
#include "Signal.h"
#include <algorithm>
#include <functional>

namespace lumen {

class NumberBox : public TextBox {
public:
    NumberBox() = default;
    explicit NumberBox(double value) { Value(value); }

    NumberBox& Range(double min_value, double max_value);
    double Min() const noexcept { return min_; }
    double Max() const noexcept { return max_; }
    // 编程赋值：钳制并写入格式化文本，不触发 OnValueChanged。
    NumberBox& Value(double value);
    // 解析当前文本；失败（空/非法）返回 false。
    bool TryParse(double& out) const;
    double Value() const;   // 解析失败回退到钳制后的 min
    // ↑↓ 步进幅度，默认 1。
    NumberBox& Step(double value) {
        step_ = std::max(1e-9, value);
        return *this;
    }
    // 显示精度（小数位），默认 0。
    NumberBox& Decimals(int value) {
        decimals_ = value < 0 ? 0 : (value > 6 ? 6 : value);
        return *this;
    }
    // 内嵌右侧 spin 区（上下小箭头），默认开启。
    NumberBox& SpinButtons(bool value) {
        spin_ = value;
        RelayoutParent();
        return *this;
    }
    NumberBox& OnValueChanged(std::function<void(double)> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindValueChanged(std::function<void(double)> handler) {
        return changed_.Connect(std::move(handler));
    }
    NumberBox& BindValue(Property<float>& p, float scale = 1.0f);
    NumberBox& BindValue(Property<double>& p);

    bool Enabled() const noexcept { return Control::Enabled(); }
    NumberBox& Enabled(bool value) {
        if (!value) {
            spin_pressed_ = -1;
            spin_hold_.Release();
        }
        Control::Enabled(value);
        return *this;
    }

protected:
    friend class WindowImpl;
    uint32_t AutomationPatterns() const noexcept override {
        return kPatternValue | kPatternRange;
    }
    double AutomationRangeValue() const override { return Value(); }
    double AutomationRangeMin() const noexcept override { return min_; }
    double AutomationRangeMax() const noexcept override { return max_; }
    double AutomationRangeSmall() const noexcept override { return step_; }
    bool AutomationSetRange(double value) override {
        if (!enabled_ || read_only_) return false;
        Value(value);
        return true;
    }
    bool AutomationIsReadOnly() const noexcept override { return read_only_; }
    TextRole ContentRole() const noexcept override { return TextRole::Numeric; }
    bool OnChar(wchar_t ch) override;
    bool ImeInline() const noexcept override { return false; }
    bool OnKey(uint32_t vk) override;
    void OnFocusChanged(bool focused) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnAnimate(float dt_seconds) override;
    CursorShape CursorAt(Point local) const override;

private:
    static constexpr float kSpinWidth = 24.0f;
    int SpinZoneAt(Point local) const;   // -1 无 / 0 上 / 1 下
    void Commit(double value, bool notify);
    void StepSpin(int zone);
    std::wstring Format(double value) const;
    float PadRight() const override;

    double min_ = 0.0;
    double max_ = 100.0;
    double step_ = 1.0;
    int decimals_ = 0;
    int spin_pressed_ = -1;
    bool spin_ = true;
    RepeatHold spin_hold_{};
    Signal<double> changed_;
    ScopedConnection value_prop_;
    ScopedConnection value_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
