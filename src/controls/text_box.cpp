#include "fluentui/TextBox.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwctype>

namespace fui {
namespace {
constexpr float kPadX = 10.0f;

IDWriteTextLayout* BodyLayout(const std::wstring& text) {
    return UiText().LineLayout(text, UiText().Format(TextRole::Body), 1.0e5f, Align::Leading);
}
} // namespace

void TextBox::RelayoutParent() { Control::RelayoutParent(); }

TextBox& TextBox::Text(std::wstring_view value) {
    text_ = std::wstring(value);
    caret_ = anchor_ = text_.size();
    scroll_x_ = 0.0f;
    Invalidate();
    return *this;
}

Size TextBox::Measure(Size, const Theme& theme) {
    return {160.0f, theme.input_height};
}

void TextBox::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    caret_on_ = true;
    blink_t_ = 0.0f;
    if (focused) {
        SetCaret(text_.size(), false, true);
        Animate();
    }
    Invalidate();
}

float TextBox::CaretX(size_t index) {
    if (text_.empty()) return 0.0f;
    float x = 0.0f, y = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (index >= text_.size()) {
        BodyLayout(text_)->HitTestTextPosition(static_cast<UINT32>(text_.size() - 1), TRUE, &x, &y,
                                               &metrics);
        x = metrics.left + metrics.width;
    } else {
        BodyLayout(text_)->HitTestTextPosition(static_cast<UINT32>(index), FALSE, &x, &y, &metrics);
        x = metrics.left;
    }
    return x;
}

void TextBox::ScrollCaretIntoView() {
    const float visible = absolute_.w - kPadX * 2.0f;
    const float caret_x = CaretX(caret_);
    if (caret_x < scroll_x_) scroll_x_ = caret_x;
    else if (caret_x > scroll_x_ + visible) scroll_x_ = caret_x - visible;
    const float text_w = text_.empty() ? 0.0f
                                       : UiText().MeasureText(text_, TextRole::Body, 0.0f).w;
    scroll_x_ = Clamp(scroll_x_, 0.0f, std::max(0.0f, text_w - visible));
}

void TextBox::SetCaret(size_t index, bool extend, bool scroll_to_caret) {
    index = Clamp(index, size_t{0}, text_.size());
    if (extend) caret_ = index;
    else caret_ = anchor_ = index;
    caret_on_ = true;
    blink_t_ = 0.0f;
    if (scroll_to_caret) ScrollCaretIntoView();
    Invalidate();
}

size_t TextBox::SelectionStart() const noexcept {
    return std::min(caret_, anchor_);
}

size_t TextBox::SelectionEnd() const noexcept {
    return std::max(caret_, anchor_);
}

void TextBox::DeleteSelection() {
    if (!HasSelection()) return;
    const size_t start = SelectionStart();
    text_.erase(start, SelectionEnd() - start);
    caret_ = anchor_ = start;
    caret_on_ = true;
    blink_t_ = 0.0f;
}

void TextBox::InsertText(const wchar_t* begin, size_t count) {
    text_.insert(caret_, begin, count);
    SetCaret(caret_ + count, false, true);
}

void TextBox::CopyOrCut(bool cut) {
    if (!HasSelection()) return;
    const std::wstring selection = text_.substr(SelectionStart(), SelectionEnd() - SelectionStart());
    if (OpenClipboard(static_cast<HWND>(NativeWindow()))) {
        EmptyClipboard();
        const size_t bytes = (selection.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            void* data = GlobalLock(memory);
            if (data) {
                memcpy(data, selection.c_str(), bytes);
                GlobalUnlock(memory);
                SetClipboardData(CF_UNICODETEXT, memory);
            } else {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }
    if (cut && !read_only_) {
        DeleteSelection();
        if (text_changed_) text_changed_();
        ScrollCaretIntoView();
        Invalidate();
    }
}

void TextBox::Paste() {
    if (read_only_ || !OpenClipboard(static_cast<HWND>(NativeWindow()))) return;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        if (const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle))) {
            std::wstring filtered;
            for (const wchar_t* p = data; *p; ++p) {
                if (*p >= 0x20 && *p != 0x7F) filtered.push_back(*p);
            }
            GlobalUnlock(handle);
            if (!filtered.empty()) {
                DeleteSelection();
                InsertText(filtered.data(), filtered.size());
                if (text_changed_) text_changed_();
            }
        }
    }
    CloseClipboard();
    ScrollCaretIntoView();
    Invalidate();
}

namespace {
size_t CaretIndexAt(const std::wstring& text, float text_x) {
    if (text.empty() || text_x <= 0.0f) return 0;
    BOOL inside = FALSE, trailing = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    BodyLayout(text)->HitTestPoint(text_x, 0.0f, &inside, &trailing, &metrics);
    size_t index = metrics.textPosition + (trailing ? 1 : 0);
    return Clamp(index, size_t{0}, text.size());
}
} // namespace

bool TextBox::OnKey(uint32_t vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    switch (vk) {
    case VK_LEFT:
        if (ctrl) {
            // 跳到词首
            size_t index = caret_;
            while (index > 0 && iswspace(text_[index - 1])) --index;
            while (index > 0 && !iswspace(text_[index - 1])) --index;
            SetCaret(index, shift);
        } else {
            SetCaret(caret_ > 0 ? caret_ - 1 : 0, shift);
        }
        return true;
    case VK_RIGHT:
        if (ctrl) {
            size_t index = caret_;
            while (index < text_.size() && !iswspace(text_[index])) ++index;
            while (index < text_.size() && iswspace(text_[index])) ++index;
            SetCaret(index, shift);
        } else {
            SetCaret(caret_ < text_.size() ? caret_ + 1 : text_.size(), shift);
        }
        return true;
    case VK_HOME:
        SetCaret(0, shift);
        return true;
    case VK_END:
        SetCaret(text_.size(), shift);
        return true;
    case VK_BACK:
        if (read_only_) return true;
        if (HasSelection()) {
            DeleteSelection();
            if (text_changed_) text_changed_();
        } else if (caret_ > 0) {
            text_.erase(caret_ - 1, 1);
            SetCaret(caret_ - 1, false, true);
            if (text_changed_) text_changed_();
        }
        ScrollCaretIntoView();
        Invalidate();
        return true;
    case VK_DELETE:
        if (read_only_) return true;
        if (HasSelection()) {
            DeleteSelection();
            if (text_changed_) text_changed_();
        } else if (caret_ < text_.size()) {
            text_.erase(caret_, 1);
            if (text_changed_) text_changed_();
        }
        Invalidate();
        return true;
    case VK_RETURN:
        if (submit_) submit_();
        return true;
    default:
        break;
    }
    if (ctrl) {
        switch (vk) {
        case 'A': anchor_ = 0; caret_ = text_.size(); Invalidate(); return true;
        case 'C': CopyOrCut(false); return true;
        case 'X': CopyOrCut(true); return true;
        case 'V': Paste(); return true;
        default: break;
        }
    }
    return false;
}

bool TextBox::OnChar(wchar_t ch) {
    if (read_only_) return false;
    if (ch < 0x20 || ch == 0x7F) return false;   // 控制字符由 OnKey 处理
    DeleteSelection();
    InsertText(&ch, 1);
    if (text_changed_) text_changed_();
    return true;
}

void TextBox::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & MK_LBUTTON)) return;
    SetFocus();
    const size_t index = CaretIndexAt(text_, local.x - kPadX + scroll_x_);
    anchor_ = caret_ = index;
    caret_on_ = true;
    blink_t_ = 0.0f;
    Invalidate();
}

void TextBox::OnMouseMove(Point local, uint32_t buttons) {
    if (!(buttons & MK_LBUTTON)) return;
    const size_t index = CaretIndexAt(text_, local.x - kPadX + scroll_x_);
    if (index != caret_) {
        caret_ = index;
        caret_on_ = true;
        blink_t_ = 0.0f;
        ScrollCaretIntoView();
        Invalidate();
    }
}

void TextBox::OnMouseUp(Point local, uint32_t buttons) {
    (void)local;
    (void)buttons;
}

bool TextBox::OnAnimate(float dt_seconds) {
    if (!focused_) return Control::OnAnimate(dt_seconds);
    blink_t_ += dt_seconds;
    if (blink_t_ >= 0.5f) {
        blink_t_ -= 0.5f;
        caret_on_ = !caret_on_;
        Invalidate();
    }
    return true;   // 保持动画时钟，光标持续闪烁
}

void TextBox::Draw(Painter& painter, const Theme& theme) {
    const Rect frame = absolute_;
    painter.FillRoundedRect(frame, theme.radius_control,
                            enabled_ ? theme.control_fill : theme.control_fill_pressed);
    painter.StrokeRoundedRect(frame, theme.radius_control, theme.control_stroke);
    if (hover_t_ > 0.01f && enabled_) {
        painter.StrokeRoundedRect(frame, theme.radius_control,
                                  Color{theme.control_stroke_strong.r, theme.control_stroke_strong.g,
                                        theme.control_stroke_strong.b,
                                        theme.control_stroke_strong.a * 0.5f * hover_t_});
    }
    if (focused_ && enabled_) {
        painter.FillRoundedRect({frame.x + 2.0f, frame.Bottom() - 2.0f, frame.w - 4.0f, 2.0f}, 1.0f,
                                theme.accent);
    }

    painter.PushClip(frame);
    const Rect content{frame.x + kPadX - scroll_x_, frame.y, 1.0e4f, frame.h};
    if (text_.empty()) {
        if (!placeholder_.empty()) {
            painter.DrawText(placeholder_, content, TextRole::Body, theme.text_secondary);
        }
    } else {
        if (HasSelection()) {
            const float start_x = CaretX(SelectionStart());
            const float end_x = CaretX(SelectionEnd());
            painter.FillRoundedRect(
                {frame.x + kPadX + start_x - scroll_x_, frame.y + 5.0f, end_x - start_x,
                 frame.h - 10.0f},
                2.0f, theme.selection);
        }
        painter.DrawText(text_, content, TextRole::Body,
                         enabled_ ? theme.text : theme.text_disabled);
    }
    if (focused_ && enabled_ && caret_on_) {
        const float caret_x = frame.x + kPadX + CaretX(caret_) - scroll_x_;
        painter.FillRect({caret_x, frame.y + 5.0f, 1.5f, frame.h - 10.0f}, theme.text);
    }
    painter.PopClip();
}

} // namespace fui
