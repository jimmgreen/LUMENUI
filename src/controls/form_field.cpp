#include "lumen/FormField.h"
#include "lumen/NumberBox.h"
#include "lumen/Painter.h"
#include "lumen/TextBox.h"
#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <regex>

namespace lumen {
namespace {
constexpr float kLabelH = 20.0f;
constexpr float kGapInline = 4.0f;
constexpr float kGapToChild = 6.0f;
constexpr float kGapChild = 8.0f;
constexpr float kGapToError = 4.0f;
constexpr float kStarW = 14.0f;
} // namespace

namespace validate {

Rule Required() {
    return Rule{[](std::wstring_view s) {
        return s.empty() ? App::Strings().required : std::wstring{};
    }};
}

Rule MinLength(size_t n) {
    return Rule{[n](std::wstring_view s) {
        return s.size() < n ? App::Strings().too_short : std::wstring{};
    }};
}

Rule Pattern(std::wstring re) {
    return Rule{[re = std::move(re)](std::wstring_view s) {
        try {
            if (std::regex_match(std::wstring(s), std::wregex(re))) return std::wstring{};
        } catch (...) {
        }
        return App::Strings().invalid_format;
    }};
}

Rule Range(double lo, double hi) {
    return Rule{[lo, hi](std::wstring_view s) {
        wchar_t* end = nullptr;
        const std::wstring tmp(s);
        const double v = std::wcstod(tmp.c_str(), &end);
        if (end == tmp.c_str() || (end && *end != 0) || !std::isfinite(v) || v < lo || v > hi) {
            return App::Strings().out_of_range;
        }
        return std::wstring{};
    }};
}

} // namespace validate

void FormField::RelayoutParent() { Control::RelayoutParent(); }

FormField& FormField::Error(std::wstring_view value) {
    if (error_ == value) return *this;
    error_ = value;
    AccessibleHelp(error_);
    for (size_t i = 0; i < ChildCount(); ++i) Child(i).AccessibleHelp(error_);
    if (form_) form_->RefreshValid();
    RelayoutParent();
    return *this;
}

FormField& FormField::Validate(validate::Rule rule) {
    validator_ = std::move(rule);
    hooked_ = false;
    validate_conns_.clear();   // ScopedConnection 析构断开旧通道
    EnsureHooked();            // 配置阶段即挂钩 + 立即按当前值求一次
    ApplyValidation(FieldText());
    RelayoutParent();
    return *this;
}

void FormField::ApplyValidation(std::wstring_view text) {
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (auto* number = dynamic_cast<NumberBox*>(&Child(i))) {
            auto error = number->DraftError();
            if (!error.empty()) { Error(error); return; }
            if (!validator_ && !required_) { Error({}); return; }
        }
    }
    if (required_ && text.empty()) { Error(App::Strings().required); return; }
    if (validator_) Error(validator_(text));
    else if (required_) Error({});
}

std::wstring FormField::FieldText() const {
    if (value_reader_) return value_reader_();
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (auto* tb = dynamic_cast<const TextBox*>(&Child(i))) return tb->Text();
    }
    return {};
}

void FormField::Revalidate() {
    EnsureHooked();
    ApplyValidation(FieldText());
}

// 挂钩时机 = 字段配置阶段（Child()/Validate()），不是首次 Measure：首次布局前
// Valid 就已可信。文本通道覆盖打字/IME；NumberBox 值提交（spin/↑↓/UIA/程序赋值）
// 走 changed_。程序直接改 TextBox::Text() 是静默契约（R13），提交前用 Form::ValidateAll。
void FormField::EnsureHooked() {
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (Child(i).AccessibleName().empty()) Child(i).AccessibleName(label_);
        Child(i).AccessibleHelp(error_);
    }
    if (hooked_) return;
    validate_conns_.clear();
    if (value_reader_) {
        hooked_ = true;
        ApplyValidation(FieldText());
        return;
    }
    for (size_t i = 0; i < ChildCount(); ++i) {
        Control& child = Child(i);
        if (auto* nb = dynamic_cast<NumberBox*>(&child)) {
            validate_conns_.push_back(
                ScopedConnection(nb->BindCommitted([this](std::optional<double>) { Revalidate(); })));
        }
        if (auto* tb = dynamic_cast<TextBox*>(&child)) {
            validate_conns_.push_back(ScopedConnection(tb->BindTextChanged(
                [this](std::wstring_view s) { ApplyValidation(s); })));
            ApplyValidation(tb->Text());
            hooked_ = true;
            return;
        }
    }
}

FormField& Form::Field(std::wstring_view label) {
    auto& field = Add<FormField>(label);
    field.AttachForm(this);
    RefreshValid();
    return field;
}

void Form::RefreshValid() {
    if (refreshing_valid_) {
        refresh_pending_ = true;
        return;
    }
    refreshing_valid_ = true;
    refresh_pending_ = false;
    cross_error_ = cross_rule_ ? cross_rule_() : std::wstring{};
    bool ok = cross_error_.empty();
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (auto* field = dynamic_cast<FormField*>(&Child(i))) {
            if (field->HasError()) {
                ok = false;
                break;
            }
        }
    }
    valid_ = ok;
    refreshing_valid_ = false;
    if (refresh_pending_)
        RefreshValid();
}

bool Form::ValidateAll() {
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (auto* field = dynamic_cast<FormField*>(&Child(i))) field->Revalidate();
    }
    RefreshValid();
    return valid_.Get();
}

bool Form::CommitAll() {
    if (!ValidateAll()) return false;
    WeakRef<Form> self(this);
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (auto* field = dynamic_cast<FormField*>(&Child(i))) {
            WeakRef<FormField> live(field);
            for (size_t j = 0; live && j < field->ChildCount(); ++j) {
                if (auto* number = dynamic_cast<NumberBox*>(&field->Child(j))) {
                    const bool committed = number->CommitValue();
                    if (!self || !live || !committed) return false;
                }
            }
        }
    }
    return ValidateAll();
}

Size FormField::Measure(Size available, const Theme& theme) {
    EnsureHooked();
    const float width = (available.w >= 0.0f && available.w < 1.0e4f) ? available.w : 280.0f;
    body_left_ = label_width_ > 0.0f && width >= label_width_ + 120.0f ? label_width_ + kGapToChild : 0.0f;
    const float label_width = body_left_ > 0.0f ? label_width_ : width;
    header_h_ = 0.0f;
    footer_h_ = 0.0f;
    desc_h_ = 0.0f;
    error_h_ = 0.0f;

    if (!label_.empty() || required_) header_h_ = kLabelH;
    if (!description_.empty()) {
        desc_h_ = MeasureWrapped(description_, TextRole::Caption, label_width);
        if (desc_h_ < 16.0f) desc_h_ = 16.0f;
        header_h_ += (header_h_ > 0.0f ? kGapInline : 0.0f) + desc_h_;
    }
    if (header_h_ > 0.0f) header_h_ += kGapToChild;

    float body = 0.0f;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {width - body_left_, 1.0e5f}, theme);
        body += desired.h + (first ? 0.0f : kGapChild);
        first = false;
    }

    if (!error_.empty()) {
        error_h_ = MeasureWrapped(error_, TextRole::Caption, width);
        if (error_h_ < 16.0f) error_h_ = 16.0f;
        footer_h_ = kGapToError + error_h_;
    }

    return {width, (body_left_ > 0.0f ? std::max(header_h_, body) : header_h_ + body) + footer_h_};
}

void FormField::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    float y = body_left_ > 0.0f ? 0.0f : header_h_;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        if (!first) y += kGapChild;
        first = false;
        const Size desired = ChildDesired(i);
        SetChildBounds(Child(i), {body_left_, y, absolute.w - body_left_, desired.h});
        ArrangeChildAt(i);
        y += desired.h;
    }
}

void FormField::Draw(Painter& painter, const Theme& theme) {
    float y = absolute_.y;
    const Color fg = enabled_ ? theme.text : theme.text_disabled;
    if (!label_.empty() || required_) {
        float x = absolute_.x;
        if (!label_.empty()) {
            const float star = required_ ? kStarW : 0.0f;
            const float label_w = std::max(0.0f, (body_left_ > 0.0f ? label_width_ : absolute_.w) - star);
            const Size ls = MeasureText(label_, TextRole::Body, label_w);
            painter.DrawText(label_, {x, y, ls.w, kLabelH}, TextRole::Body, fg);
            x += ls.w;
        }
        if (required_) {
            painter.DrawText(L" *", {x, y, kStarW, kLabelH}, TextRole::Body, theme.accent);
        }
        y += kLabelH;
    }
    if (!description_.empty()) {
        if (y > absolute_.y) y += kGapInline;
        painter.DrawTextWrapped(description_, {absolute_.x, y, body_left_ > 0.0f ? label_width_ : absolute_.w, desc_h_},
                                TextRole::Caption, theme.text_secondary);
    }
    if (!error_.empty()) {
        painter.DrawTextWrapped(error_,
                                {absolute_.x, absolute_.Bottom() - error_h_, absolute_.w, error_h_},
                                TextRole::Caption, theme.danger);
    }
}

} // namespace lumen
