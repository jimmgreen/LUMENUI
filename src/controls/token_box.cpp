#include "lumen/TokenBox.h"
#include "lumen/Chip.h"
#include "lumen/Painter.h"
#include "lumen/TextBox.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace lumen {
namespace {

constexpr float kPadH = 8.0f;
constexpr float kPadV = 6.0f;
constexpr float kGap = 6.0f;
constexpr float kChipH = 28.0f;
constexpr float kFieldMin = 80.0f;
constexpr float kInf = 1.0e5f;

bool AxisFinite(float v) noexcept { return v >= 0.0f && v < 1.0e4f; }

bool IsDelimiter(wchar_t ch) noexcept {
    return ch == L',' || ch == L';' || ch == L'，' || ch == L'；';
}

std::wstring Trim(std::wstring_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(static_cast<wint_t>(text[begin]))) ++begin;
    while (end > begin && std::iswspace(static_cast<wint_t>(text[end - 1]))) --end;
    return std::wstring(text.substr(begin, end - begin));
}

size_t FieldIndex(Panel& panel, Control* field) {
    for (size_t i = 0; i < panel.ChildCount(); ++i) {
        if (&panel.Child(i) == field) return i;
    }
    return panel.ChildCount();
}

} // namespace

class TokenBox::Field : public TextBox {
public:
    TokenBox* host = nullptr;

    void TakeFocus() { Focus(); }

    bool PaintChrome() const noexcept override { return false; }
    float PadLeft() const override { return 4.0f; }
    float PadRight() const override { return 4.0f; }

    Size Measure(Size, const Theme&) override {
        float width = MeasureText(text_, TextRole::Body).w;
        if (!ime_comp_.empty()) width += MeasureText(ime_comp_, TextRole::Body).w;
        width += PadLeft() + PadRight() + 2.0f;
        return {std::max(kFieldMin, width), kChipH};
    }

    bool OnKey(uint32_t vk) override {
        if (!host || !enabled_) return TextBox::OnKey(vk);
        if (ImeComposing()) return TextBox::OnKey(vk);
        if (vk == VK_BACK && text_.empty() && !HasSelection()) {
            host->RemoveLast();
            return true;
        }
        if (vk == VK_RETURN) {
            host->CommitDraft();
            return true;
        }
        return TextBox::OnKey(vk);
    }

    bool OnChar(wchar_t ch) override {
        if (host && enabled_ && !ImeComposing() && IsDelimiter(ch)) {
            host->CommitDraft();
            return true;
        }
        return TextBox::OnChar(ch);
    }
};

TokenBox::TokenBox() {
    Clip(true);
    field_ = &Add<Field>();
    field_->host = this;
    field_->Placeholder(placeholder_);
    field_->OnTextChanged([this](std::wstring_view) {
        SplitDraft();
        Relayout();
    });
}

TokenBox& TokenBox::Enabled(bool value) {
    Control::Enabled(value);
    for (size_t i = 0; i < ChildCount(); ++i) Child(i).Enabled(value);
    return *this;
}

void TokenBox::OnMouseDown(Point, uint32_t) {
    if (enabled_ && field_) field_->TakeFocus();
}

const std::wstring& TokenBox::Draft() const noexcept {
    static const std::wstring kEmpty;
    return field_ ? field_->Text() : kEmpty;
}

TokenBox& TokenBox::Draft(std::wstring_view text) {
    if (field_) field_->Text(text);
    Relayout();
    return *this;
}

TokenBox& TokenBox::Placeholder(std::wstring_view value) {
    placeholder_ = value;
    SyncPlaceholder();
    return *this;
}

void TokenBox::SyncPlaceholder() {
    if (!field_) return;
    field_->Placeholder(tokens_.empty() ? placeholder_ : std::wstring{});
}

bool TokenBox::AtLimit() const noexcept {
    return max_tokens_ > 0 && tokens_.size() >= max_tokens_;
}

Chip* TokenBox::ChipAt(size_t token_index) {
    size_t seen = 0;
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (&Child(i) == field_ || !ChildVisible(i)) continue;
        if (seen == token_index) return static_cast<Chip*>(&Child(i));
        ++seen;
    }
    return nullptr;
}

void TokenBox::CompactHidden() {
    for (size_t i = ChildCount(); i-- > 0;) {
        Control& child = Child(i);
        if (&child == field_ || child.Visible()) continue;
        Remove(child);
    }
}

void TokenBox::InsertChip(std::wstring_view text) {
    auto& chip = Add<Chip>(text);
    chip.Closable(true).Enabled(enabled_);
    chip.OnClosed([this, &chip] {
        size_t seen = 0;
        for (size_t i = 0; i < ChildCount(); ++i) {
            if (&Child(i) == field_ || !ChildVisible(i)) continue;
            if (&Child(i) == &chip) {
                if (seen < tokens_.size()) {
                    tokens_.erase(tokens_.begin() + static_cast<ptrdiff_t>(seen));
                }
                chip.Visible(false);
                SyncPlaceholder();
                NotifyChanged();
                Relayout();
                if (field_) field_->TakeFocus();
                return;
            }
            ++seen;
        }
    });
    if (children_.size() >= 2) {
        std::rotate(children_.end() - 2, children_.end() - 1, children_.end());
    }
}

bool TokenBox::AddToken(std::wstring_view text) {
    std::wstring token = Trim(text);
    if (token.empty() || AtLimit()) return false;
    if (!allow_duplicates_) {
        for (const std::wstring& existing : tokens_) {
            if (existing == token) return false;
        }
    }
    CompactHidden();
    tokens_.push_back(token);
    InsertChip(token);
    SyncPlaceholder();
    NotifyChanged();
    Relayout();
    return true;
}

bool TokenBox::RemoveTokenAt(size_t index) {
    if (index >= tokens_.size()) return false;
    if (Chip* chip = ChipAt(index)) chip->Visible(false);
    tokens_.erase(tokens_.begin() + static_cast<ptrdiff_t>(index));
    SyncPlaceholder();
    NotifyChanged();
    Relayout();
    return true;
}

bool TokenBox::RemoveToken(std::wstring_view text) {
    for (size_t i = 0; i < tokens_.size(); ++i) {
        if (tokens_[i] == text) return RemoveTokenAt(i);
    }
    return false;
}

bool TokenBox::RemoveLast() {
    if (tokens_.empty()) return false;
    return RemoveTokenAt(tokens_.size() - 1);
}

void TokenBox::ClearTokens() {
    if (tokens_.empty()) return;
    tokens_.clear();
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (&Child(i) != field_) Child(i).Visible(false);
    }
    SyncPlaceholder();
    NotifyChanged();
    Relayout();
}

TokenBox& TokenBox::Tokens(std::vector<std::wstring> value) {
    Signal<> notify = std::move(changed_);
    ClearTokens();
    CompactHidden();
    for (const std::wstring& item : value) AddToken(item);
    changed_ = std::move(notify);
    NotifyChanged();
    return *this;
}

void TokenBox::SplitDraft() {
    if (!field_) return;
    const std::wstring raw = field_->Text();
    bool has_delim = false;
    for (wchar_t ch : raw) {
        if (IsDelimiter(ch)) {
            has_delim = true;
            break;
        }
    }
    if (!has_delim) return;
    field_->Text(L"");
    std::wstring cur;
    for (wchar_t ch : raw) {
        if (IsDelimiter(ch)) {
            AddToken(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    field_->Text(cur);
}

bool TokenBox::CommitDraft() {
    if (!field_) return false;
    SplitDraft();
    const bool added = AddToken(field_->Text());
    if (added || Trim(field_->Text()).empty()) field_->Text(L"");
    Relayout();
    return added;
}

void TokenBox::NotifyChanged() {
    changed_.Emit();
}

Size TokenBox::Measure(Size available, const Theme& theme) {
    CompactHidden();
    const bool finite = AxisFinite(available.w);
    const float inner = finite ? std::max(0.0f, available.w - kPadH * 2.0f) : kInf;

    const size_t field_index = FieldIndex(*this, field_);
    Size field_d{kFieldMin, kChipH};
    if (field_index < ChildCount()) field_d = MeasureChildAt(field_index, {kInf, kInf}, theme);

    float line_w = 0.0f;
    float used_w = 0.0f;
    int rows = 0;
    auto end_line = [&] {
        used_w = std::max(used_w, line_w);
        ++rows;
        line_w = 0.0f;
    };
    auto add_chip = [&](float width) {
        if (line_w > 0.0f && line_w + kGap + width > inner + 0.01f) end_line();
        if (line_w > 0.0f) line_w += kGap;
        line_w += width;
    };
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (&Child(i) == field_ || !ChildVisible(i)) continue;
        add_chip(MeasureChildAt(i, {kInf, kInf}, theme).w);
    }
    if (finite && line_w > 0.0f && inner - line_w - kGap < kFieldMin) end_line();
    const float gap = line_w > 0.0f ? kGap : 0.0f;
    float field_w = field_d.w;
    if (finite) field_w = std::max(field_d.w, inner - line_w - gap);
    line_w += gap + field_w;
    end_line();
    if (rows < 1) rows = 1;

    const float height = kPadV * 2.0f + static_cast<float>(rows) * kChipH +
                         static_cast<float>(rows - 1) * kGap;
    const float width = finite ? available.w : used_w + kPadH * 2.0f;
    return {width, std::max(theme.input_height, height)};
}

void TokenBox::Arrange(const Rect& absolute) {
    CompactHidden();
    absolute_ = absolute;
    const float inner = std::max(0.0f, absolute.w - kPadH * 2.0f);
    float x = kPadH;
    float y = kPadV;
    auto new_line = [&] {
        x = kPadH;
        y += kChipH + kGap;
    };
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (&Child(i) == field_ || !ChildVisible(i)) continue;
        const Size desired = ChildDesired(i);
        if (x > kPadH && x + desired.w > kPadH + inner + 0.01f) new_line();
        SetChildBounds(Child(i), {x, y, desired.w, kChipH});
        ArrangeChildAt(i);
        x += desired.w + kGap;
    }
    const size_t field_index = FieldIndex(*this, field_);
    if (field_index >= ChildCount()) return;
    const Size field_desired = ChildDesired(field_index);
    float remain = kPadH + inner - x;
    if (x > kPadH && remain < kFieldMin) {
        new_line();
        remain = inner;
    }
    const float field_w = std::max(field_desired.w, std::max(0.0f, remain));
    SetChildBounds(*field_, {x, y, field_w, kChipH});
    ArrangeChildAt(field_index);
}

void TokenBox::Draw(Painter& painter, const Theme& theme) {
    const bool lit = field_ && field_->HasFocus();
    Color fill = theme.fill_input;
    Color stroke = theme.control_stroke;
    if (!enabled_) {
        fill = theme.fill_input_disabled;
    } else if (lit) {
        fill = theme.fill_input_focus;
        stroke = theme.accent;
        painter.DrawGlow(absolute_, theme.radius_control, theme.glow_sm);
    } else if (hovered_) {
        fill = theme.fill_input_hover;
    }
    painter.FillRoundedRect(absolute_, theme.radius_control, fill);
    if (enabled_) {
        painter.DrawInnerLight(absolute_, theme.radius_control, theme.edge_light,
                               Color{0.0f, 0.0f, 0.0f, 0.35f});
    }
    painter.StrokeRoundedRect(absolute_, theme.radius_control, stroke);
}

TokenBox& TokenBox::BindTokens(Property<std::vector<std::wstring>>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Tokens(p.Get());
        bind_loop_ = false;
    };
    apply();
    tokens_prop_ = ScopedConnection(p.OnChanged([apply](const std::vector<std::wstring>&) { apply(); }));
    tokens_ctrl_ = ScopedConnection(changed_.Connect([this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = tokens_;
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
