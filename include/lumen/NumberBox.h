// lumen/NumberBox.h — 数字输入框：字符过滤、↑↓ 步进、失焦提交钳制格式化、右侧 spin 区。
// Events: OnValueChanged / BindValueChanged
// Keys: 焦点控件处理 ↑↓ 步进等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "TextBox.h"
#include "Animate.h"
#include "Signal.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

namespace lumen {

class NumberBox : public TextBox {
public:
    NumberBox() = default;
    explicit NumberBox(double value) { Value(value); }

    NumberBox& Range(double min_value, double max_value);
    double Min() const noexcept { return min_; }
    double Max() const noexcept { return max_; }
    // 编程赋值：有限值钳制并格式化，不触发 OnValueChanged；非有限值忽略。
    NumberBox& Value(double value);
    NumberBox& Nullable(bool on = true) { nullable_ = on; return *this; }
    NumberBox& Integer(bool on = true) { integer_ = on; return *this; }
    NumberBox& ClampOnCommit(bool on = true) { clamp_on_commit_ = on; return *this; }
    NumberBox& CommitOnBlur(bool on = true) { commit_on_blur_ = on; return *this; }
    NumberBox& Unit(std::wstring_view text) { unit_ = text; RelayoutParent(); return *this; }
    const std::wstring& Error() const noexcept { return error_; }
    std::optional<double> OptionalValue() const { return nullable_ && text_.empty() ? std::nullopt : std::optional<double>(Value()); }
    void ClearValue();
    bool CommitValue();
    std::wstring DraftError() const;
    NumberBox& OnCommitted(std::function<void(std::optional<double>)> fn) { committed_event_.Subscribe(std::move(fn)); return *this; }
    Connection BindCommitted(std::function<void(std::optional<double>)> fn) { return committed_event_.Connect(std::move(fn)); }
    // 严格解析当前草稿：完整消费且有限的数值才成功；空/`12abc`/溢出返回 false。
    bool TryParse(double& out) const;
    // 草稿合法时返回钳制后的草稿值；非法/空时返回最近有效值（首次为 min），
    // 不把未确认草稿静默当成下界。用户提交走 OnValueChanged。
    double Value() const;
    // ↑↓ 从当前合法草稿步进；非法草稿从最近提交值步进。幅度默认 1。
    NumberBox& Step(double value) {
        if (std::isfinite(value)) step_ = std::max(1e-9, value);
        return *this;
    }
    // 显示精度（小数位），默认 0。
    NumberBox& Decimals(int value) {
        decimals_ = value < 0 ? 0 : (value > 6 ? 6 : value);
        if (has_committed_) Text(Format(committed_));
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
    NumberBox& BindValue(Property<std::optional<double>>& p);

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
    Size Measure(Size available, const Theme& theme) override;
    friend class WindowImpl;
    uint32_t AutomationPatterns() const noexcept override {
        return kPatternValue | kPatternRange;
    }
    double AutomationRangeValue() const override { return Value(); }
    double AutomationRangeMin() const noexcept override { return min_; }
    double AutomationRangeMax() const noexcept override { return max_; }
    double AutomationRangeSmall() const noexcept override { return step_; }
    bool AutomationSetRange(double value) override;
    bool AutomationSetValue(std::wstring_view value) override;
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
    static bool ParseText(const std::wstring& text, double& out);
    static constexpr float kSpinWidth = 24.0f;
    int SpinZoneAt(Point local) const;   // -1 无 / 0 上 / 1 下
    bool Commit(double value, bool notify);
    void StepSpin(int zone);
    double LastCommitted() const noexcept;
    std::wstring Format(double value) const;
    float PadRight() const override;

    double min_ = 0.0;
    double max_ = 100.0;
    double step_ = 1.0;
    int decimals_ = 0;
    int spin_pressed_ = -1;
    bool spin_ = true;
    double committed_ = 0.0;    // 最近一次提交的有效值（含编程赋值）
    bool has_committed_ = false;
    bool nullable_ = false, integer_ = false, clamp_on_commit_ = true, commit_on_blur_ = true;
    bool committed_null_ = false;
    std::wstring unit_, error_;
    float unit_width_ = 0.0f;
    Signal<std::optional<double>> committed_event_;
    RepeatHold spin_hold_{};
    Signal<double> changed_;
    ScopedConnection value_prop_;
    ScopedConnection value_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
