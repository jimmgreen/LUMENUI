#include "lumen/FormField.h"
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
        if (end == tmp.c_str() || (end && *end != 0) || v < lo || v > hi) {
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
    if (form_) form_->RefreshValid();
    RelayoutParent();
    return *this;
}

FormField& FormField::Validate(validate::Rule rule) {
    validator_ = std::move(rule);
    hooked_ = false;
    validate_conn_.Disconnect();
    RelayoutParent();
    return *this;
}

void FormField::ApplyValidation(std::wstring_view text) {
    if (!validator_) return;
    Error(validator_(text));
}

void FormField::EnsureHooked() {
    if (hooked_ || !validator_) return;
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (auto* tb = dynamic_cast<TextBox*>(&Child(i))) {
            validate_conn_ = ScopedConnection(tb->BindTextChanged([this](std::wstring_view s) {
                ApplyValidation(s);
            }));
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
    bool ok = true;
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (auto* field = dynamic_cast<FormField*>(&Child(i))) {
            if (field->HasError()) {
                ok = false;
                break;
            }
        }
    }
    valid_ = ok;
}

Size FormField::Measure(Size available, const Theme& theme) {
    EnsureHooked();
    const float width = (available.w >= 0.0f && available.w < 1.0e4f) ? available.w : 280.0f;
    header_h_ = 0.0f;
    footer_h_ = 0.0f;
    desc_h_ = 0.0f;
    error_h_ = 0.0f;

    if (!label_.empty() || required_) header_h_ = kLabelH;
    if (!description_.empty()) {
        desc_h_ = MeasureWrapped(description_, TextRole::Caption, width);
        if (desc_h_ < 16.0f) desc_h_ = 16.0f;
        header_h_ += (header_h_ > 0.0f ? kGapInline : 0.0f) + desc_h_;
    }
    if (header_h_ > 0.0f) header_h_ += kGapToChild;

    float body = 0.0f;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {width, 1.0e5f}, theme);
        body += desired.h + (first ? 0.0f : kGapChild);
        first = false;
    }

    if (!error_.empty()) {
        error_h_ = MeasureWrapped(error_, TextRole::Caption, width);
        if (error_h_ < 16.0f) error_h_ = 16.0f;
        footer_h_ = kGapToError + error_h_;
    }

    return {width, header_h_ + body + footer_h_};
}

void FormField::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    float y = header_h_;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        if (!first) y += kGapChild;
        first = false;
        const Size desired = ChildDesired(i);
        SetChildBounds(Child(i), {0.0f, y, absolute.w, desired.h});
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
            const float label_w = std::max(0.0f, absolute_.w - star);
            const Size ls = MeasureText(label_, TextRole::BodyStrong, label_w);
            painter.DrawText(label_, {x, y, ls.w, kLabelH}, TextRole::BodyStrong, fg);
            x += ls.w;
        }
        if (required_) {
            painter.DrawText(L" *", {x, y, kStarW, kLabelH}, TextRole::BodyStrong, theme.accent);
        }
        y += kLabelH;
    }
    if (!description_.empty()) {
        if (y > absolute_.y) y += kGapInline;
        painter.DrawTextWrapped(description_, {absolute_.x, y, absolute_.w, desc_h_},
                                TextRole::Caption, theme.text_secondary);
    }
    if (!error_.empty()) {
        painter.DrawTextWrapped(error_,
                                {absolute_.x, absolute_.Bottom() - error_h_, absolute_.w, error_h_},
                                TextRole::Caption, theme.danger);
    }
}

} // namespace lumen
