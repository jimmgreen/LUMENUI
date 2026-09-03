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

    using Panel::Child;

    template <typename T, typename = std::enable_if_t<std::is_base_of_v<Control, std::decay_t<T>>>>
    FormField& Child(T&& x) {
        AdoptOne(std::forward<T>(x));
        hooked_ = false;
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

    std::wstring label_;
    std::wstring description_;
    std::wstring error_;
    validate::Rule validator_;
    Form* form_ = nullptr;
    ScopedConnection validate_conn_;
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

private:
    Property<bool> valid_{true};
};

} // namespace lumen
