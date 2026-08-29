// fluentui/TextBox.h — 单行文本框：光标、选区、剪贴板、占位符。
#pragma once
#include "Control.h"
#include <functional>
#include <string>

namespace fui {

class TextBox : public Control {
public:
    TextBox() = default;
    explicit TextBox(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    TextBox& Text(std::wstring_view value);          // 不触发 OnTextChanged
    const std::wstring& Placeholder() const noexcept { return placeholder_; }
    TextBox& Placeholder(std::wstring_view value) { placeholder_ = value; Invalidate(); return *this; }
    bool ReadOnly() const noexcept { return read_only_; }
    TextBox& ReadOnly(bool value) { read_only_ = value; return *this; }

    void OnTextChanged(std::function<void()> handler) { text_changed_ = std::move(handler); }
    void OnSubmit(std::function<void()> handler) { submit_ = std::move(handler); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    void OnFocusChanged(bool focused) override;
    bool OnKey(uint32_t vk) override;
    bool OnChar(wchar_t ch) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    void InsertText(const wchar_t* begin, size_t count);
    void DeleteSelection();
    size_t SelectionStart() const noexcept;   // 较小端
    size_t SelectionEnd() const noexcept;     // 较大端
    bool HasSelection() const noexcept { return caret_ != anchor_; }
    void SetCaret(size_t index, bool extend = false, bool scroll_to_caret = true);
    float CaretX(size_t index);               // 相对文本起点（DIP）
    void ScrollCaretIntoView();
    void CopyOrCut(bool cut);
    void Paste();

    std::wstring text_;
    std::wstring placeholder_;
    size_t caret_ = 0;        // 光标字符索引 [0, len]
    size_t anchor_ = 0;       // 选区锚点
    float scroll_x_ = 0.0f;
    bool read_only_ = false;
    bool caret_on_ = true;
    float blink_t_ = 0.0f;
    std::function<void()> text_changed_;
    std::function<void()> submit_;
};

} // namespace fui
