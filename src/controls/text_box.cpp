#include "lumen/TextBox.h"
#include "lumen/App.h"
#include "lumen/Clipboard.h"
#include "lumen/Icons.h"
#include "lumen/Menu.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <imm.h>
#include <algorithm>
#include <cstring>
#include <cwctype>

namespace lumen {
namespace {
constexpr float kPadX = 14.0f;
constexpr float kPadY = 12.0f;
constexpr float kGlyphSlot = 24.0f;
constexpr float kFloatBand = 18.0f;
constexpr float kFloatInset = 8.0f;
constexpr size_t kUndoLimit = 64;
constexpr DWORD kUndoMergeMs = 1000;

bool IsHighSurrogate(wchar_t ch) noexcept {
    return ch >= 0xD800 && ch <= 0xDBFF;
}

bool IsLowSurrogate(wchar_t ch) noexcept {
    return ch >= 0xDC00 && ch <= 0xDFFF;
}

uint32_t CodePointAt(std::wstring_view text, size_t index, size_t* length = nullptr) noexcept {
    if (index >= text.size()) {
        if (length) *length = 0;
        return 0;
    }
    const wchar_t first = text[index];
    if (IsHighSurrogate(first) && index + 1 < text.size() && IsLowSurrogate(text[index + 1])) {
        if (length) *length = 2;
        return 0x10000u + ((static_cast<uint32_t>(first) - 0xD800u) << 10) +
               (static_cast<uint32_t>(text[index + 1]) - 0xDC00u);
    }
    if (length) *length = 1;
    return static_cast<uint32_t>(first);
}

bool IsVariationSelector(uint32_t cp) noexcept {
    return (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool IsEmojiModifier(uint32_t cp) noexcept {
    return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

bool IsRegionalIndicator(uint32_t cp) noexcept {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

bool IsCombiningMark(uint32_t cp) noexcept {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

size_t NextCodePoint(std::wstring_view text, size_t index) noexcept {
    size_t length = 0;
    CodePointAt(text, index, &length);
    return std::min(index + std::max<size_t>(length, 1), text.size());
}

size_t PrevCodePoint(std::wstring_view text, size_t index) noexcept {
    index = std::min(index, text.size());
    if (index == 0) return 0;
    --index;
    if (IsLowSurrogate(text[index]) && index > 0 && IsHighSurrogate(text[index - 1])) --index;
    return index;
}

size_t NextTextElement(std::wstring_view text, size_t index) noexcept {
    index = std::min(index, text.size());
    if (index >= text.size()) return text.size();
    const uint32_t first_cp = CodePointAt(text, index);
    size_t next = NextCodePoint(text, index);
    if (IsRegionalIndicator(first_cp) && IsRegionalIndicator(CodePointAt(text, next))) {
        next = NextCodePoint(text, next);
    }
    for (;;) {
        const uint32_t cp = CodePointAt(text, next);
        if (IsVariationSelector(cp) || IsEmojiModifier(cp) || IsCombiningMark(cp)) {
            next = NextCodePoint(text, next);
            continue;
        }
        if (cp == 0x200D && next < text.size()) {
            next = NextCodePoint(text, next);
            if (next < text.size()) next = NextCodePoint(text, next);
            continue;
        }
        return next;
    }
}

size_t PrevTextElement(std::wstring_view text, size_t index) noexcept {
    index = std::min(index, text.size());
    if (index == 0) return 0;
    size_t scan = 0;
    size_t previous = 0;
    while (scan < index) {
        previous = scan;
        const size_t next = NextTextElement(text, scan);
        if (next >= index || next <= scan) return previous;
        scan = next;
    }
    return previous;
}

size_t SnapTextElement(std::wstring_view text, size_t index) noexcept {
    index = std::min(index, text.size());
    size_t scan = 0;
    size_t previous = 0;
    while (scan < index) {
        previous = scan;
        const size_t next = NextTextElement(text, scan);
        if (next >= index || next <= scan) {
            return index - previous < next - index ? previous : next;
        }
        scan = next;
    }
    return scan;
}

bool IsWordChar(wchar_t ch) noexcept { return iswspace(static_cast<wint_t>(ch)) == 0; }

bool MaskIsLiteral(wchar_t slot) noexcept {
    return slot != L'0' && slot != L'9' && slot != L'A' && slot != L'a' && slot != L'*' &&
           slot != L'_';
}

bool MaskMatches(wchar_t slot, wchar_t ch) noexcept {
    switch (slot) {
    case L'0':
    case L'9':
        return ch >= L'0' && ch <= L'9';
    case L'A':
    case L'a':
        return iswalpha(static_cast<wint_t>(ch)) != 0;
    case L'*':
    case L'_':
        return ch >= 0x20 && ch != 0x7F;
    default:
        return ch == slot;
    }
}

void PaintField(Painter& painter, const Theme& theme, const Rect& r, float radius, bool enabled,
                bool focused, bool hovered) {
    Color fill = theme.fill_input;
    Color stroke = theme.control_stroke;
    if (!enabled) {
        fill = theme.fill_input_disabled;
    } else if (focused) {
        fill = theme.fill_input_focus;
        stroke = theme.accent;
        painter.DrawGlow(r, radius, theme.glow_sm);
    } else if (hovered) {
        fill = theme.fill_input_hover;
    }
    painter.FillRoundedRect(r, radius, fill);
    if (enabled) {
        painter.DrawInnerLight(r, radius, theme.edge_light, Color{0.0f, 0.0f, 0.0f, 0.35f});
    }
    painter.StrokeRoundedRect(r, radius, stroke);
}

void CancelOsIme(void* native_hwnd) {
    HWND hwnd = static_cast<HWND>(native_hwnd);
    if (!hwnd) return;
    if (HIMC himc = ImmGetContext(hwnd)) {
        ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(hwnd, himc);
    }
}

// ATTR_INPUT / TARGET_NOTCONVERTED：点划线；CONVERTED：实线。
void PaintImeUnderline(Painter& painter, float x, float y, float width, Color color, bool dotted) {
    if (width <= 0.0f) return;
    if (!dotted) {
        painter.FillRect({x, y, width, 1.0f}, color);
        return;
    }
    for (float dx = 0.0f; dx < width; dx += 3.5f) {
        painter.FillRect({x + dx, y, std::min(2.0f, width - dx), 1.0f}, color);
    }
}
} // namespace

void TextBox::RelayoutParent() { Control::RelayoutParent(); }

float TextBox::PadLeft() const {
    return glyph_.empty() ? kPadX : kPadX + kGlyphSlot;
}

float TextBox::PadTop() const {
    if (multiline_) return kPadY;
    return floating_label_ && !placeholder_.empty() ? kFloatBand : 0.0f;
}

float TextBox::PadRight() const { return kPadX; }

float TextBox::LineHeight() const {
    // 与 Painter::DrawTextWrapped / MeasureWrappedHeight 同一探针，避免行高漂移导致点选错位。
    return std::max(MeasureText(L"m4B", ContentRole()).h, 18.0f);
}

size_t TextBox::HardLineCount() const noexcept {
    const std::wstring& shown = VisibleText();
    if (shown.empty()) return 1;
    size_t lines = 1;
    for (wchar_t ch : shown) {
        if (ch == L'\n') ++lines;
    }
    return lines;
}

float TextBox::ContentWidth() const {
    return std::max(0.0f, absolute_.w - PadLeft() - PadRight());
}

TextBox& TextBox::Text(std::wstring_view value) {
    ClearCompose();
    text_.assign(value);
    pending_high_surrogate_ = 0;
    if (password_) multiline_ = false;
    caret_ = anchor_ = text_.size();
    scroll_x_ = scroll_y_ = 0.0f;
    undo_.clear();
    redo_.clear();
    last_op_ = EditOp::None;
    RefreshMask();
    Invalidate();
    return *this;
}

TextBox& TextBox::Password(bool value) {
    if (password_ == value) return *this;
    password_ = value;
    if (password_) {
        multiline_ = false;
        floating_label_ = false;
    }
    RefreshMask();
    Invalidate();
    return *this;
}

TextBox& TextBox::Multiline(bool value) {
    if (password_) value = false;
    if (multiline_ == value) return *this;
    multiline_ = value;
    if (multiline_) floating_label_ = false;
    scroll_x_ = scroll_y_ = 0.0f;
    RelayoutParent();
    Invalidate();
    return *this;
}

TextBox& TextBox::MaxLength(size_t n) {
    max_length_ = n;
    if (max_length_ > 0 && text_.size() > max_length_) {
        text_.resize(max_length_);
        caret_ = std::min(caret_, text_.size());
        anchor_ = std::min(anchor_, text_.size());
        RefreshMask();
        Invalidate();
    }
    return *this;
}

TextBox& TextBox::Mask(std::wstring_view pattern) {
    input_mask_.assign(pattern);
    if (!input_mask_.empty() && max_length_ == 0) {
        // 掩码长度即上限；显式 MaxLength 仍可再收紧。
    }
    Invalidate();
    return *this;
}

TextBox& TextBox::FloatingLabel(bool value) {
    value = value && !multiline_ && !password_;
    if (floating_label_ == value) return *this;
    floating_label_ = value;
    if (!floating_label_) float_t_ = 0.0f;
    else Animate();
    RelayoutParent();
    Invalidate();
    return *this;
}

void TextBox::RefreshMask() {
    if (password_) mask_.assign(text_.size(), L'\u25CF');
    else mask_.clear();
}

const std::wstring& TextBox::VisibleText() const noexcept {
    return password_ ? mask_ : text_;
}

void TextBox::NotifyChanged() {
    RefreshMask();
    last_op_tick_ = GetTickCount();
    text_changed_.Emit(text_);
    ScrollCaretIntoView();
    if (floating_label_) Animate();
    EnsureEditMenu();
    Invalidate();
}

void TextBox::PushUndo(EditOp op) {
    const DWORD now = GetTickCount();
    const bool merge = (op == EditOp::Type || op == EditOp::Erase) && op == last_op_ &&
                       !undo_.empty() && now - last_op_tick_ <= kUndoMergeMs;
    last_op_ = op;
    last_op_tick_ = now;
    if (merge) return;
    Snapshot snap{text_, caret_, anchor_};
    if (!undo_.empty() && undo_.back().text == snap.text && undo_.back().caret == snap.caret &&
        undo_.back().anchor == snap.anchor) {
        return;
    }
    undo_.push_back(std::move(snap));
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
}

bool TextBox::Undo() {
    if (undo_.empty() || read_only_) return false;
    redo_.push_back({text_, caret_, anchor_});
    const Snapshot snap = undo_.back();
    undo_.pop_back();
    text_ = snap.text;
    caret_ = snap.caret;
    anchor_ = snap.anchor;
    NotifyChanged();
    NotifyImeCaret();
    last_op_ = EditOp::None;
    return true;
}

bool TextBox::Redo() {
    if (redo_.empty() || read_only_) return false;
    undo_.push_back({text_, caret_, anchor_});
    const Snapshot snap = redo_.back();
    redo_.pop_back();
    text_ = snap.text;
    caret_ = snap.caret;
    anchor_ = snap.anchor;
    NotifyChanged();
    NotifyImeCaret();
    last_op_ = EditOp::None;
    return true;
}

Size TextBox::Measure(Size available, const Theme& theme) {
    if (!multiline_) {
        const float extra = (floating_label_ && !placeholder_.empty()) ? kFloatBand : 0.0f;
        return {160.0f, theme.input_height + extra};
    }
    const float width = (available.w > 1.0f && available.w < 1.0e4f) ? available.w : 280.0f;
    // 多行编辑按硬换行排版（与 HitIndex/Caret 一致）；软换行会让绘制行与点选错位。
    const float text_h = LineHeight() * static_cast<float>(std::max<size_t>(3, HardLineCount()));
    return {width, text_h + kPadY * 2.0f};
}

void TextBox::OnFocusChanged(bool focused) {
    if (!focused) {
        pending_high_surrogate_ = 0;
        if (!ime_comp_.empty()) {
            ClearCompose();
            CancelOsIme(NativeWindow());
        }
    }
    caret_on_ = true;
    blink_t_ = 0.0f;
    if (focused) {
        Animate();
        NotifyImeCaret();
        EnsureEditMenu();
    }
    Invalidate();
}

void TextBox::NotifyImeCaret() {
    if (window_ && focused_) WindowImpl::SyncImeCaret(window_);
}

void TextBox::ClearCompose() {
    if (ime_comp_.empty() && ime_attr_.empty() && ime_cursor_ == 0) return;
    ime_comp_.clear();
    ime_attr_.clear();
    ime_cursor_ = 0;
    Invalidate();
}

float TextBox::VisualCaretX() const {
    float x = CaretX(caret_);
    if (!ime_comp_.empty()) {
        const size_t cursor = std::min(ime_cursor_, ime_comp_.size());
        x += TextAdvance(std::wstring_view(ime_comp_).substr(0, cursor));
    }
    return x;
}

void TextBox::OnImeCompose(std::wstring_view text, size_t cursor, std::string_view attributes) {
    if (read_only_) return;
    if (text.empty()) {
        ClearCompose();
        NotifyImeCaret();
        return;
    }
    if (HasSelection()) {
        PushUndo();
        DeleteSelection();
        NotifyChanged();
    }
    ime_comp_.assign(text);
    ime_attr_.assign(attributes);
    ime_cursor_ = std::min(cursor, ime_comp_.size());
    caret_on_ = true;
    blink_t_ = 0.0f;
    ScrollCaretIntoView();
    NotifyImeCaret();
    Invalidate();
}

void TextBox::OnImeCommit(std::wstring_view text) {
    ClearCompose();
    if (read_only_ || text.empty()) {
        NotifyImeCaret();
        return;
    }
    PushUndo();
    DeleteSelection();
    InsertText(text.data(), text.size());
    NotifyChanged();
    NotifyImeCaret();
}

void TextBox::OnImeEnd() {
    ClearCompose();
    NotifyImeCaret();
}

void TextBox::DrawComposition(Painter& painter, const Theme& theme, float x, float text_y,
                              float text_h, float band_y, float band_h) const {
    if (ime_comp_.empty()) return;
    const Color color = enabled_ ? theme.text : theme.text_disabled;
    const std::wstring_view comp = ime_comp_;
    float acc = 0.0f;
    size_t i = 0;
    while (i < comp.size()) {
        const unsigned attr = i < ime_attr_.size() ? static_cast<unsigned char>(ime_attr_[i]) : 0;
        size_t j = i + 1;
        while (j < comp.size()) {
            const unsigned next =
                j < ime_attr_.size() ? static_cast<unsigned char>(ime_attr_[j]) : 0;
            if (next != attr) break;
            ++j;
        }
        const float w = TextAdvance(comp.substr(i, j - i));
        // 1 = ATTR_TARGET_CONVERTED，3 = ATTR_TARGET_NOTCONVERTED
        if (attr == 1 || attr == 3) {
            painter.FillRoundedRect(
                {x + acc, band_y + 2.0f, std::max(1.0f, w), std::max(1.0f, band_h - 4.0f)}, 2.0f,
                theme.fill_selected);
        }
        acc += w;
        i = j;
    }
    painter.DrawText(comp, {x, text_y, 1.0e4f, text_h}, ContentRole(), color);
    acc = 0.0f;
    i = 0;
    const float uy = band_y + band_h - 2.0f;
    while (i < comp.size()) {
        const unsigned attr = i < ime_attr_.size() ? static_cast<unsigned char>(ime_attr_[i]) : 0;
        size_t j = i + 1;
        while (j < comp.size()) {
            const unsigned next =
                j < ime_attr_.size() ? static_cast<unsigned char>(ime_attr_[j]) : 0;
            if (next != attr) break;
            ++j;
        }
        const float w = TextAdvance(comp.substr(i, j - i));
        const bool dotted = attr == 0 || attr == 3 || attr == 4;
        PaintImeUnderline(painter, x + acc, uy, w, color, dotted);
        acc += w;
        i = j;
    }
}

size_t TextBox::SelectionStart() const noexcept { return std::min(caret_, anchor_); }
size_t TextBox::SelectionEnd() const noexcept { return std::max(caret_, anchor_); }

size_t TextBox::LineStart(size_t index) const {
    index = std::min(index, text_.size());
    while (index > 0 && text_[index - 1] != L'\n') --index;
    return index;
}

size_t TextBox::LineEnd(size_t index) const {
    index = std::min(index, text_.size());
    while (index < text_.size() && text_[index] != L'\n') ++index;
    return index;
}

size_t TextBox::WordLeft(size_t index) const {
    index = std::min(index, text_.size());
    while (index > 0 && !IsWordChar(text_[index - 1])) --index;
    while (index > 0 && IsWordChar(text_[index - 1])) --index;
    return index;
}

size_t TextBox::WordRight(size_t index) const {
    index = std::min(index, text_.size());
    while (index < text_.size() && IsWordChar(text_[index])) ++index;
    while (index < text_.size() && !IsWordChar(text_[index])) ++index;
    return index;
}

void TextBox::SelectWordAt(size_t index) {
    index = std::min(index, text_.size());
    if (text_.empty()) {
        caret_ = anchor_ = 0;
        Invalidate();
        return;
    }
    if (index == text_.size()) --index;
    size_t start = index;
    size_t end = index;
    if (IsWordChar(text_[index])) {
        while (start > 0 && IsWordChar(text_[start - 1])) --start;
        while (end < text_.size() && IsWordChar(text_[end])) ++end;
    } else {
        while (start > 0 && !IsWordChar(text_[start - 1])) --start;
        while (end < text_.size() && !IsWordChar(text_[end])) ++end;
    }
    anchor_ = start;
    caret_ = end;
    caret_on_ = true;
    blink_t_ = 0.0f;
    ScrollCaretIntoView();
    NotifyImeCaret();
    Invalidate();
}

void TextBox::SelectLineAt(size_t index) {
    anchor_ = LineStart(index);
    caret_ = LineEnd(index);
    if (caret_ < text_.size() && text_[caret_] == L'\n') ++caret_;
    caret_on_ = true;
    blink_t_ = 0.0f;
    ScrollCaretIntoView();
    NotifyImeCaret();
    Invalidate();
}

void TextBox::ApplyClick(Point local, uint8_t count) {
    const size_t hit = HitIndex(local);
    if (count >= 3) SelectLineAt(hit);
    else if (count == 2) SelectWordAt(hit);
    else SetCaret(hit, false, true);
}

void TextBox::EnsureEditMenu() {
    const bool has_sel = HasSelection() && !password_;
    const bool can_edit = enabled_ && !read_only_;
    const bool can_copy = has_sel;
    const bool can_paste = can_edit;
    const auto& strings = App::Strings();
    Menu menu;
    menu.AddItem(strings.cut, [this] { CopyOrCut(true); })
        .Glyph(icon::kCut)
        .Shortcut(L"Ctrl+X")
        .Disabled(!can_copy || !can_edit);
    menu.AddItem(strings.copy, [this] { CopyOrCut(false); })
        .Glyph(icon::kCopy)
        .Shortcut(L"Ctrl+C")
        .Disabled(!can_copy);
    menu.AddItem(strings.paste, [this] { Paste(); })
        .Glyph(icon::kPaste)
        .Shortcut(L"Ctrl+V")
        .Disabled(!can_paste);
    menu.AddSeparator();
    menu.AddItem(strings.select_all, [this] {
        anchor_ = 0;
        caret_ = text_.size();
        NotifyImeCaret();
        Invalidate();
    }).Shortcut(L"Ctrl+A").Disabled(text_.empty());
    ContextMenu(std::move(menu));
}

void TextBox::MoveSelectionTo(size_t dest) {
    if (!HasSelection() || read_only_ || password_) return;
    dest = std::min(dest, text_.size());
    const size_t start = SelectionStart();
    const size_t end = SelectionEnd();
    if (dest >= start && dest <= end) return;
    PushUndo(EditOp::Other);
    const std::wstring chunk = text_.substr(start, end - start);
    text_.erase(start, end - start);
    if (dest > start) dest -= chunk.size();
    dest = std::min(dest, text_.size());
    text_.insert(dest, chunk);
    caret_ = dest + chunk.size();
    anchor_ = dest;
    NotifyChanged();
    NotifyImeCaret();
}

void TextBox::InsertMasked(wchar_t ch) {
    auto skip_literals = [this] {
        while (caret_ < input_mask_.size() && MaskIsLiteral(input_mask_[caret_])) {
            const wchar_t lit = input_mask_[caret_];
            if (caret_ >= text_.size()) text_.push_back(lit);
            else if (text_[caret_] != lit) text_.insert(caret_, 1, lit);
            ++caret_;
        }
        anchor_ = caret_;
    };
    skip_literals();
    if (caret_ >= input_mask_.size()) return;
    if (max_length_ > 0 && text_.size() >= max_length_) return;
    const wchar_t slot = input_mask_[caret_];
    if (MaskIsLiteral(slot) || !MaskMatches(slot, ch)) return;
    if (caret_ < text_.size()) text_[caret_] = ch;
    else text_.push_back(ch);
    ++caret_;
    skip_literals();
}

float TextBox::CaretX(size_t index) const {
    const std::wstring& shown = VisibleText();
    index = std::min(index, shown.size());
    if (!multiline_) {
        float x = 0.0f;
        if (window_ && WindowImpl::CaretXBody(window_, shown, index, &x, ContentRole())) return x;
        return TextAdvance(shown.substr(0, index));
    }
    const size_t start = LineStart(index);
    return TextAdvance(shown.substr(start, index - start));
}

float TextBox::TextAdvance(std::wstring_view text) const {
    if (text.empty()) return 0.0f;
    float x = 0.0f;
    if (window_ && WindowImpl::CaretXBody(window_, text, text.size(), &x, ContentRole())) return x;
    return UiText().MeasureText(text, ContentRole()).w;
}

float TextBox::CaretY(size_t index) const {
    if (!multiline_) return 0.0f;
    size_t line = 0;
    const size_t n = std::min(index, text_.size());
    for (size_t i = 0; i < n; ++i) {
        if (text_[i] == L'\n') ++line;
    }
    return static_cast<float>(line) * LineHeight();
}

void TextBox::ScrollCaretIntoView() {
    const float pad = PadLeft();
    const float top = PadTop();
    const float extra = ime_comp_.empty() ? 0.0f : TextAdvance(ime_comp_);
    if (!multiline_) {
        const float visible = absolute_.w - pad - PadRight();
        const float caret_x = VisualCaretX();
        if (caret_x < scroll_x_) scroll_x_ = caret_x;
        else if (caret_x > scroll_x_ + visible) scroll_x_ = caret_x - visible;
        const float text_w = TextAdvance(VisibleText()) + extra;
        scroll_x_ = Clamp(scroll_x_, 0.0f, std::max(0.0f, text_w - visible));
        scroll_y_ = 0.0f;
        return;
    }
    const float visible_w = ContentWidth();
    const float visible_h = std::max(0.0f, absolute_.h - top - kPadY);
    const float caret_x = VisualCaretX();
    const float caret_y = CaretY(caret_);
    if (caret_x < scroll_x_) scroll_x_ = caret_x;
    else if (caret_x > scroll_x_ + visible_w) scroll_x_ = caret_x - visible_w;
    if (caret_y < scroll_y_) scroll_y_ = caret_y;
    else if (caret_y + LineHeight() > scroll_y_ + visible_h) {
        scroll_y_ = caret_y + LineHeight() - visible_h;
    }
    const float content_h = LineHeight() * static_cast<float>(HardLineCount());
    const float text_w = [&] {
        float max_w = 0.0f;
        size_t start = 0;
        const std::wstring& shown = VisibleText();
        for (size_t i = 0; i <= shown.size(); ++i) {
            if (i == shown.size() || shown[i] == L'\n') {
                float line_w = TextAdvance(shown.substr(start, i - start));
                if (caret_ >= start && caret_ <= i) line_w += extra;
                max_w = std::max(max_w, line_w);
                start = i + 1;
            }
        }
        return max_w;
    }();
    scroll_x_ = Clamp(scroll_x_, 0.0f, std::max(0.0f, text_w - visible_w));
    scroll_y_ = Clamp(scroll_y_, 0.0f, std::max(0.0f, content_h - visible_h));
}

void TextBox::SetCaret(size_t index, bool extend, bool scroll_to_caret) {
    index = SnapTextElement(text_, index);
    caret_ = index;
    if (!extend) anchor_ = index;
    caret_on_ = true;
    blink_t_ = 0.0f;
    if (scroll_to_caret) ScrollCaretIntoView();
    NotifyImeCaret();
    Invalidate();
}

void TextBox::DeleteSelection() {
    if (!HasSelection()) return;
    const size_t start = SelectionStart();
    text_.erase(start, SelectionEnd() - start);
    caret_ = anchor_ = start;
}

void TextBox::InsertText(const wchar_t* begin, size_t count) {
    if (!begin || count == 0) return;
    if (!input_mask_.empty() && !password_) {
        for (size_t i = 0; i < count; ++i) InsertMasked(begin[i]);
        return;
    }
    size_t n = count;
    if (max_length_ > 0) {
        if (text_.size() >= max_length_) return;
        n = std::min(n, max_length_ - text_.size());
    }
    if (n == 0) return;
    text_.insert(caret_, begin, n);
    caret_ = anchor_ = caret_ + n;
}

size_t TextBox::HitIndex(Point local) const {
    const std::wstring& shown = VisibleText();
    const float x = local.x - PadLeft() + scroll_x_;
    if (!multiline_) {
        size_t index = 0;
        if (window_ && WindowImpl::HitTestBody(window_, shown, x, &index, ContentRole())) {
            return index;
        }
        float acc = 0.0f;
        for (size_t i = 0; i < shown.size();) {
            const size_t next = NextTextElement(shown, i);
            const float w = TextAdvance(shown.substr(i, next - i));
            if (x < acc + w * 0.5f) return i;
            acc += w;
            i = next;
        }
        return SnapTextElement(shown, shown.size());
    }
    const float y = local.y - PadTop() + scroll_y_;
    const float lh = LineHeight();
    const size_t hard_lines = HardLineCount();
    const size_t line = std::min(static_cast<size_t>(std::max(0.0f, y) / std::max(lh, 1.0f)),
                                 hard_lines > 0 ? hard_lines - 1 : 0);
    size_t start = 0;
    size_t current = 0;
    for (size_t i = 0; i <= shown.size(); ++i) {
        if (current == line) {
            start = i;
            break;
        }
        if (i < shown.size() && shown[i] == L'\n') {
            ++current;
            start = i + 1;
        }
        if (i == shown.size()) {
            start = shown.size();
            break;
        }
    }
    const size_t end = LineEnd(start);
    const std::wstring line_text = shown.substr(start, end - start);
    float acc = 0.0f;
    for (size_t i = 0; i < line_text.size();) {
        const size_t next = NextTextElement(line_text, i);
        const float w = TextAdvance(line_text.substr(i, next - i));
        if (x < acc + w * 0.5f) return start + i;
        acc += w;
        i = next;
    }
    return start + SnapTextElement(line_text, line_text.size());
}

void TextBox::CopyOrCut(bool cut) {
    if (!HasSelection() || password_) return;
    const std::wstring selection = text_.substr(SelectionStart(), SelectionEnd() - SelectionStart());
    clipboard::Text(selection);
    if (cut && !read_only_) {
        PushUndo();
        DeleteSelection();
        NotifyChanged();
        NotifyImeCaret();
    }
}

void TextBox::Paste() {
    pending_high_surrogate_ = 0;
    if (read_only_) return;
    const std::wstring data = clipboard::Text();
    if (data.empty()) return;
    std::wstring filtered;
    for (wchar_t ch : data) {
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            if (multiline_) filtered.push_back(L'\n');
            continue;
        }
        if (ch >= 0x20) filtered.push_back(ch);
    }
    if (!filtered.empty()) {
        PushUndo();
        DeleteSelection();
        InsertText(filtered.data(), filtered.size());
        NotifyChanged();
        NotifyImeCaret();
    }
}

bool TextBox::OnKey(uint32_t vk) {
    pending_high_surrogate_ = 0;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (!ime_comp_.empty()) {
        // 组字中退格/方向/空格/回车交给 IME；快捷键吞掉以免 Undo 把预编辑冲掉。
        return ctrl;
    }
    if (ctrl && vk == 'Z' && !shift) return Undo();
    if (ctrl && (vk == 'Y' || (vk == 'Z' && shift))) return Redo();

    switch (vk) {
    case VK_LEFT:
        if (ctrl) SetCaret(WordLeft(caret_), shift);
        else SetCaret(PrevTextElement(text_, caret_), shift);
        return true;
    case VK_RIGHT:
        if (ctrl) SetCaret(WordRight(caret_), shift);
        else SetCaret(NextTextElement(text_, caret_), shift);
        return true;
    case VK_UP:
        if (!multiline_) return false;
        {
            const size_t start = LineStart(caret_);
            if (start == 0) {
                SetCaret(0, shift);
            } else {
                const size_t prev_end = start - 1;
                const size_t prev_start = LineStart(prev_end);
                const size_t col = caret_ - start;
                SetCaret(std::min(prev_start + col, prev_end), shift);
            }
        }
        return true;
    case VK_DOWN:
        if (!multiline_) return false;
        {
            const size_t start = LineStart(caret_);
            const size_t end = LineEnd(caret_);
            if (end >= text_.size()) {
                SetCaret(text_.size(), shift);
            } else {
                const size_t next_start = end + 1;
                const size_t next_end = LineEnd(next_start);
                const size_t col = caret_ - start;
                SetCaret(std::min(next_start + col, next_end), shift);
            }
        }
        return true;
    case VK_HOME:
        SetCaret(multiline_ ? LineStart(caret_) : 0, shift);
        return true;
    case VK_END:
        SetCaret(multiline_ ? LineEnd(caret_) : text_.size(), shift);
        return true;
    case VK_BACK:
        if (read_only_) return true;
        if (HasSelection()) {
            PushUndo(EditOp::Erase);
            DeleteSelection();
        } else if (caret_ > 0) {
            PushUndo(EditOp::Erase);
            size_t previous = ctrl ? WordLeft(caret_) : PrevTextElement(text_, caret_);
            if (!input_mask_.empty() && !ctrl && previous < input_mask_.size() &&
                MaskIsLiteral(input_mask_[previous]) && previous > 0) {
                previous = PrevTextElement(text_, previous);
            }
            text_.erase(previous, caret_ - previous);
            caret_ = anchor_ = previous;
        } else {
            return true;
        }
        NotifyChanged();
        NotifyImeCaret();
        return true;
    case VK_DELETE:
        if (read_only_) return true;
        if (HasSelection()) {
            PushUndo(EditOp::Erase);
            DeleteSelection();
        } else if (caret_ < text_.size()) {
            PushUndo(EditOp::Erase);
            size_t next = ctrl ? WordRight(caret_) : NextTextElement(text_, caret_);
            if (!input_mask_.empty() && !ctrl && caret_ < input_mask_.size() &&
                MaskIsLiteral(input_mask_[caret_]) && next < text_.size()) {
                next = NextTextElement(text_, next);
            }
            text_.erase(caret_, next - caret_);
            anchor_ = caret_;
        } else {
            return true;
        }
        NotifyChanged();
        NotifyImeCaret();
        return true;
    case VK_RETURN:
        if (multiline_ && !ctrl) {
            if (read_only_) return true;
            PushUndo(EditOp::Other);
            DeleteSelection();
            const wchar_t nl = L'\n';
            InsertText(&nl, 1);
            NotifyChanged();
            NotifyImeCaret();
            return true;
        }
        submit_.Emit();
        return true;
    default:
        break;
    }
    if (ctrl) {
        switch (vk) {
        case 'A':
            anchor_ = 0;
            caret_ = text_.size();
            NotifyImeCaret();
            Invalidate();
            return true;
        case 'C':
            CopyOrCut(false);
            return true;
        case 'X':
            CopyOrCut(true);
            return true;
        case 'V':
            Paste();
            return true;
        default:
            break;
        }
    }
    return false;
}

bool TextBox::OnChar(wchar_t ch) {
    if (read_only_ || !ime_comp_.empty()) return false;
    if (ch < 0x20 || ch == 0x7F) return false;
    if (IsHighSurrogate(ch)) {
        pending_high_surrogate_ = ch;
        return true;
    }
    wchar_t pair[2]{};
    const wchar_t* input = &ch;
    size_t count = 1;
    if (IsLowSurrogate(ch) && pending_high_surrogate_) {
        pair[0] = pending_high_surrogate_;
        pair[1] = ch;
        input = pair;
        count = 2;
    } else if (IsLowSurrogate(ch)) {
        pending_high_surrogate_ = 0;
        return true;
    }
    pending_high_surrogate_ = 0;
    PushUndo(IsWordChar(ch) ? EditOp::Type : EditOp::Other);
    DeleteSelection();
    InsertText(input, count);
    NotifyChanged();
    NotifyImeCaret();
    return true;
}

bool TextBox::ImeCaret(Point& window_dip, float& height_dip) const {
    const bool float_slot = floating_label_ && !placeholder_.empty() && !multiline_;
    const float x = absolute_.x + PadLeft() + VisualCaretX() - scroll_x_;
    const float y =
        absolute_.y + PadTop() + CaretY(caret_) - scroll_y_ + (float_slot ? 4.0f : 0.0f);
    window_dip = {Clamp(x, absolute_.x, absolute_.Right()),
                  Clamp(y, absolute_.y, absolute_.Bottom())};
    if (multiline_) {
        height_dip = LineHeight();
    } else if (float_slot) {
        height_dip = std::max(absolute_.h - PadTop() - 8.0f, 1.0f);
    } else {
        height_dip = std::max(absolute_.h - 16.0f, 1.0f);
    }
    return true;
}

CursorShape TextBox::CursorAt(Point) const { return CursorShape::IBeam; }

bool TextBox::AcceptsTextDrop() const noexcept {
    return enabled_ && !read_only_ && !password_;
}

void TextBox::OnTextDrop(std::wstring_view text, Point window_dip) {
    if (!AcceptsTextDrop() || text.empty()) return;
    const Point local{window_dip.x - absolute_.x, window_dip.y - absolute_.y};
    Focus();
    SetCaret(HitIndex(local), false, true);
    if (!ole_source_) PushUndo(EditOp::Other);
    DeleteSelection();
    InsertText(text.data(), text.size());
    drop_inserted_ = true;
    NotifyChanged();
    NotifyImeCaret();
}

void TextBox::OnMouseDown(Point local, uint32_t buttons) {
    EnsureEditMenu();
    if (buttons & MK_RBUTTON) {
        Focus();
        const size_t hit = HitIndex(local);
        if (!HasSelection() || hit < SelectionStart() || hit >= SelectionEnd()) {
            SetCaret(hit, false, true);
        }
        return;
    }
    if (!(buttons & MK_LBUTTON)) return;
    if (!ime_comp_.empty()) {
        ClearCompose();
        CancelOsIme(NativeWindow());
    }
    Focus();
    const DWORD now = GetTickCount();
    const float dx = local.x - last_click_local_.x;
    const float dy = local.y - last_click_local_.y;
    if (now - last_click_tick_ <= GetDoubleClickTime() && dx * dx + dy * dy < 25.0f) {
        click_count_ = static_cast<uint8_t>(std::min(3, click_count_ + 1));
    } else {
        click_count_ = 1;
    }
    last_click_tick_ = now;
    last_click_local_ = local;

    const size_t hit = HitIndex(local);
    if (click_count_ == 1 && HasSelection() && !password_ && hit >= SelectionStart() &&
        hit < SelectionEnd()) {
        selection_drag_ = true;
        selection_dragging_ = false;
        drag_origin_ = local;
        drop_preview_ = hit;
        return;
    }
    selection_drag_ = false;
    drop_preview_ = static_cast<size_t>(-1);
    ApplyClick(local, click_count_);
}

void TextBox::OnMouseDoubleClick(Point local) {
    click_count_ = 2;
    last_click_tick_ = GetTickCount();
    last_click_local_ = local;
    SelectWordAt(HitIndex(local));
}

void TextBox::OnMouseMove(Point local, uint32_t buttons) {
    if (!(buttons & MK_LBUTTON)) return;
    if (selection_drag_) {
        const float dx = local.x - drag_origin_.x;
        const float dy = local.y - drag_origin_.y;
        if (!selection_dragging_ && dx * dx + dy * dy > 16.0f) selection_dragging_ = true;
        if (!selection_dragging_) return;
        const bool outside = local.x < 0.0f || local.y < 0.0f || local.x > absolute_.w ||
                             local.y > absolute_.h;
        if (outside && window_ && !read_only_) {
            const size_t start = SelectionStart();
            const size_t end = SelectionEnd();
            const std::wstring chunk = text_.substr(start, end - start);
            selection_drag_ = false;
            selection_dragging_ = false;
            drop_preview_ = static_cast<size_t>(-1);
            drop_inserted_ = false;
            ole_source_ = true;
            PushUndo(EditOp::Other);
            const DWORD effect = WindowImpl::DragUnicodeText(chunk);
            ole_source_ = false;
            if ((effect & DROPEFFECT_MOVE) != 0) {
                if (drop_inserted_) {
                    const size_t dest = caret_ >= chunk.size() ? caret_ - chunk.size() : 0;
                    size_t origin = start;
                    if (dest <= origin) origin += chunk.size();
                    if (dest < origin || dest >= origin + chunk.size()) {
                        text_.erase(origin, chunk.size());
                        if (origin < caret_) caret_ -= chunk.size();
                        anchor_ = caret_;
                    }
                } else {
                    text_.erase(start, end - start);
                    caret_ = anchor_ = std::min(start, text_.size());
                }
                NotifyChanged();
                NotifyImeCaret();
            }
            Invalidate();
            return;
        }
        const size_t at = HitIndex(local);
        if (at != drop_preview_) {
            drop_preview_ = at;
            Invalidate();
        }
        return;
    }
    SetCaret(HitIndex(local), true, true);
}

void TextBox::OnMouseUp(Point local, uint32_t) {
    if (selection_drag_) {
        if (selection_dragging_) MoveSelectionTo(HitIndex(local));
        else SetCaret(HitIndex(local), false, true);
        selection_drag_ = false;
        selection_dragging_ = false;
        drop_preview_ = static_cast<size_t>(-1);
        Invalidate();
    }
    EnsureEditMenu();
}

bool TextBox::OnAnimate(float dt_seconds) {
    bool more = Control::OnAnimate(dt_seconds);
    if (floating_label_ && !placeholder_.empty()) {
        const float target =
            (focused_ || !text_.empty() || !ime_comp_.empty()) ? 1.0f : 0.0f;
        const float before = float_t_;
        more |= EaseTo(float_t_, target, dt_seconds, 18.0f);
        if (float_t_ != before) Invalidate();
    }
    if (!focused_) return more;
    blink_t_ += dt_seconds;
    if (blink_t_ >= 0.5f) {
        blink_t_ -= 0.5f;
        caret_on_ = !caret_on_;
        Invalidate();
    }
    return true;
}

void TextBox::Draw(Painter& painter, const Theme& theme) {
    const Rect frame = absolute_;
    if (PaintChrome()) {
        PaintField(painter, theme, frame, theme.radius_control, enabled_, focused_, hovered_);
    }

    const float pad = PadLeft();
    const float top = PadTop();
    const bool float_slot = floating_label_ && !placeholder_.empty() && !multiline_;
    const float content_y = float_slot ? frame.y + top : frame.y;
    const float content_h = float_slot ? std::max(1.0f, frame.h - top) : frame.h;
    const float line_caret_y = float_slot ? content_y + 4.0f : frame.y + 8.0f;
    const float line_caret_h = float_slot ? std::max(1.0f, content_h - 8.0f) : frame.h - 16.0f;
    if (!glyph_.empty()) {
        const Color icon_color = focused_ && enabled_ ? theme.text : theme.text_secondary;
        const float icon_y = float_slot ? content_y : frame.y;
        const float icon_h = float_slot ? content_h : (multiline_ ? 28.0f : frame.h);
        painter.DrawIcon(glyph_, {frame.x + kPadX, icon_y, kGlyphSlot, icon_h}, 16.0f, icon_color);
    }

    if (floating_label_ && !placeholder_.empty()) {
        const float t = float_t_;
        Color c = theme.text_secondary;
        if (focused_ && enabled_) {
            c = theme.text;
        }
        const TextRole role = t > 0.45f ? TextRole::Caption : ContentRole();
        const Size cap = painter.MeasureText(placeholder_, TextRole::Caption);
        const float dest_h = std::max(cap.h, 12.0f);
        const Rect lr{frame.x + pad, frame.y + kFloatInset * t, std::max(8.0f, ContentWidth()),
                      frame.h + (dest_h - frame.h) * t};
        painter.DrawText(placeholder_, lr, role, c);
    }

    painter.PushClip(frame.Inset(1.0f, 1.0f));
    const bool composing = !ime_comp_.empty();
    const bool show_inline_ph = text_.empty() && !composing && !placeholder_.empty() &&
                                !floating_label_;
    if (show_inline_ph) {
        if (!placeholder_.empty()) {
            const Rect ph{frame.x + pad, frame.y + top, ContentWidth(),
                          multiline_ ? std::max(0.0f, frame.h - top - kPadY) : frame.h};
            if (multiline_) {
                painter.DrawTextWrapped(placeholder_, ph, ContentRole(), theme.text_secondary);
            } else {
                painter.DrawText(placeholder_, ph, ContentRole(), theme.text_secondary);
            }
        }
    } else if (multiline_) {
        if (HasSelection() && !composing) {
            size_t i = SelectionStart();
            const size_t end = SelectionEnd();
            while (i < end) {
                const size_t line_end = std::min(LineEnd(i), end);
                const float x0 = CaretX(i);
                const float x1 = CaretX(line_end);
                const float y = CaretY(i);
                painter.FillRoundedRect(
                    {frame.x + pad + x0 - scroll_x_, frame.y + top + y - scroll_y_,
                     std::max(2.0f, x1 - x0), LineHeight()},
                    2.0f, theme.fill_selected);
                if (line_end >= text_.size() || text_[line_end] != L'\n') break;
                i = line_end + 1;
            }
        }
        // 按 \\n 逐行绘制，与 HitIndex/Caret 共用硬换行模型（软换行会导致末行点选错位）。
        const std::wstring& shown = VisibleText();
        const std::wstring_view view = shown;
        const Color color = enabled_ ? theme.text : theme.text_disabled;
        const float lh = LineHeight();
        const size_t caret = std::min(caret_, shown.size());
        size_t start = 0;
        size_t line = 0;
        for (size_t i = 0; i <= shown.size(); ++i) {
            if (i == shown.size() || shown[i] == L'\n') {
                const float row_y = frame.y + top + static_cast<float>(line) * lh - scroll_y_;
                const float row_x = frame.x + pad - scroll_x_;
                const Rect row{row_x, row_y, 1.0e5f, lh};
                if (composing && caret >= start && caret <= i) {
                    if (caret > start) {
                        painter.DrawText(view.substr(start, caret - start), row, ContentRole(),
                                         color);
                    }
                    const float insert_x = frame.x + pad + CaretX(caret_) - scroll_x_;
                    DrawComposition(painter, theme, insert_x, row_y, lh, row_y, lh);
                    if (i > caret) {
                        const float suffix_x =
                            insert_x + TextAdvance(ime_comp_);
                        painter.DrawText(view.substr(caret, i - caret),
                                         {suffix_x, row_y, 1.0e5f, lh}, ContentRole(), color);
                    }
                } else if (i > start) {
                    painter.DrawText(view.substr(start, i - start), row, ContentRole(), color);
                }
                if (i == shown.size()) break;
                start = i + 1;
                ++line;
            }
        }
    } else {
        const std::wstring_view shown = VisibleText();
        const Color color = enabled_ ? theme.text : theme.text_disabled;
        if (HasSelection() && !composing) {
            const float x0 = CaretX(SelectionStart());
            const float x1 = CaretX(SelectionEnd());
            painter.FillRoundedRect({frame.x + pad + x0 - scroll_x_, line_caret_y,
                                     std::max(1.0f, x1 - x0), line_caret_h},
                                    2.0f, theme.fill_selected);
        }
        if (composing) {
            const float origin_x = frame.x + pad - scroll_x_;
            const size_t caret = std::min(caret_, shown.size());
            if (caret > 0) {
                painter.DrawText(shown.substr(0, caret),
                                 {origin_x, content_y, 1.0e4f, content_h}, ContentRole(), color);
            }
            const float insert_x = frame.x + pad + CaretX(caret_) - scroll_x_;
            DrawComposition(painter, theme, insert_x, content_y, content_h, line_caret_y,
                            line_caret_h);
            if (caret < shown.size()) {
                const float suffix_x =
                    insert_x + TextAdvance(ime_comp_);
                painter.DrawText(shown.substr(caret), {suffix_x, content_y, 1.0e4f, content_h},
                                 ContentRole(), color);
            }
        } else {
            painter.DrawText(shown, {frame.x + pad - scroll_x_, content_y, 1.0e4f, content_h},
                             ContentRole(), color);
        }
    }
    if (focused_ && enabled_ && caret_on_ && !read_only_) {
        const float caret_x = frame.x + pad + VisualCaretX() - scroll_x_;
        const float caret_y =
            multiline_ ? frame.y + top + CaretY(caret_) - scroll_y_ : line_caret_y;
        const float caret_h = multiline_ ? LineHeight() : line_caret_h;
        painter.FillRect({caret_x, caret_y, 1.5f, caret_h}, theme.text);
    }
    if (selection_dragging_ && drop_preview_ != static_cast<size_t>(-1) && !read_only_) {
        const float caret_x = frame.x + pad + CaretX(drop_preview_) - scroll_x_;
        const float caret_y =
            multiline_ ? frame.y + top + CaretY(drop_preview_) - scroll_y_ : line_caret_y;
        const float caret_h = multiline_ ? LineHeight() : line_caret_h;
        painter.FillRect({caret_x, caret_y, 1.5f, caret_h}, theme.accent);
    }
    painter.PopClip();
}

TextBox& TextBox::BindText(Property<std::wstring>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        Text(p.Get());
        bind_loop_ = false;
    };
    apply();
    text_prop_ = ScopedConnection(p.OnChanged([apply](const std::wstring&) { apply(); }));
    text_ctrl_ = ScopedConnection(text_changed_.Connect([this, &p](std::wstring_view v) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = std::wstring(v);
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
