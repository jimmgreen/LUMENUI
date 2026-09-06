// lumen/FormField.h — 校验字段：标签 + 必填 + 错误文案，包任意子控件。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "App.h"
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class Form;
class TextBox;

namespace validate {
struct Rule {
    std::function<std::wstring(std::wstring_view)> fn;
    Rule() = default;
    Rule(std::function<std::wstring(std::wstring_view)> f) : fn(std::move(f)) {}
    std::wstring operator()(std::wstring_view s) const { return fn ? fn(s) : std::wstring{}; }
    explicit operator bool() const noexcept { return static_cast<bool>(fn); }
};

Rule Required();
Rule MinLength(size_t n);
Rule Pattern(std::wstring re);
Rule Range(double lo, double hi);

inline Rule operator|(Rule a, Rule b) {
    return Rule{[a = std::move(a), b = std::move(b)](std::wstring_view s) {
        std::wstring err = a(s);
        if (!err.empty()) return err;
        return b(s);
    }};
}
} // namespace validate

class FormField : public PanelOf<FormField> {
public:
    FormField() = default;
    explicit FormField(std::wstring_view label) : label_(label) {}

    FormField& Label(std::wstring_view value) {
        label_ = value;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Label() const noexcept { return label_; }
    FormField& Caption(std::wstring_view value) { return Label(value); }
    const std::wstring& Caption() const noexcept { return Label(); }

    FormField& Description(std::wstring_view value) {
        description_ = value;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Description() const noexcept { return description_; }

    FormField& Required(bool value) {
        if (required_ == value) return *this;
        required_ = value;
        hooked_ = false;
        RelayoutParent();
        return *this;
    }
    bool Required() const noexcept { return required_; }

    FormField& Error(std::wstring_view value);
    const std::wstring& Error() const noexcept { return error_; }
    bool HasError() const noexcept { return !error_.empty(); }

    FormField& Validate(validate::Rule rule);
    FormField& LabelWidth(float width) { label_width_ = std::max(0.0f, width); RelayoutParent(); return *this; }
    FormField& ValueSource(std::function<std::wstring()> read) { value_reader_ = std::move(read); Revalidate(); return *this; }
    template<class T>
    FormField& Watch(Property<T>& value) {
        auto self = std::make_shared<WeakRef<FormField>>(this);
        external_conns_.emplace_back(value.OnChanged([self](const T&) { if (*self) (*self)->Revalidate(); }));
        Revalidate(); return *this;
    }
    FormField& Observe(Connection connection) { external_conns_.emplace_back(std::move(connection)); return *this; }

    // 立即用当前子控件值重跑校验（程序赋值是静默的，改完值后调用；
    // NumberBox 的值提交/步进会自动重验）。无校验规则时 no-op。
    void Revalidate();

    using Panel::Child;

    template <typename T, typename = std::enable_if_t<std::is_base_of_v<Control, std::decay_t<T>>>>
    FormField& Child(T&& x) {
        AdoptOne(std::forward<T>(x));
        hooked_ = false;
        EnsureHooked();   // 配置阶段即挂钩：首次布局前 Valid 就可信
        Relayout();
        return *this;
    }

protected:
    friend class WindowImpl;
    friend class Form;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;

    void RelayoutParent();
    void AttachForm(Form* form) noexcept { form_ = form; }
    void EnsureHooked();
    void ApplyValidation(std::wstring_view text);
    std::wstring FieldText() const;

    std::wstring label_;
    std::wstring description_;
    std::wstring error_;
    validate::Rule validator_;
    std::function<std::wstring()> value_reader_;
    std::vector<ScopedConnection> external_conns_;
    float label_width_ = 0.0f, body_left_ = 0.0f;
    Form* form_ = nullptr;
    std::vector<ScopedConnection> validate_conns_;   // 文本输入 + 数字值提交双通道
    float header_h_ = 0.0f;
    float footer_h_ = 0.0f;
    float desc_h_ = 0.0f;
    float error_h_ = 0.0f;
    bool required_ = false;
    bool hooked_ = false;
};

class Form : public Column {
public:
    FormField& Field(std::wstring_view label);
    Property<bool>& Valid() noexcept { return valid_; }
    const Property<bool>& Valid() const noexcept { return valid_; }
    void RefreshValid();
    // 提交校验：对所有字段立即重跑规则（覆盖程序赋值、数字步进等静默路径），
    // 返回整表当前是否有效。业务提交前调用，不依赖下一次布局。
    bool ValidateAll();
    bool CommitAll();
    Form& Validate(std::function<std::wstring()> rule) { cross_rule_ = std::move(rule); RefreshValid(); return *this; }
    const std::wstring& Error() const noexcept { return cross_error_; }

private:
    Property<bool> valid_{true};
    std::function<std::wstring()> cross_rule_;
    std::wstring cross_error_;
    bool refreshing_valid_ = false;
    bool refresh_pending_ = false;
};

} // namespace lumen
