// lumen/TextBox.h — 文本框：光标、选区、剪贴板、占位符；可选多行与撤销。
// Events: OnTextChanged / BindTextChanged / OnSubmit / BindSubmit
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class TextBox : public ControlOf<TextBox> {
public:
    TextBox() = default;
    explicit TextBox(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    TextBox& Text(std::wstring_view value);  // 不触发 OnTextChanged，不入撤销栈
    TextBox& Text(std::string_view utf8) { return Text(U8(utf8)); }
    const std::wstring& Placeholder() const noexcept { return placeholder_; }
    TextBox& Placeholder(std::wstring_view value) {
        placeholder_ = value;
        Invalidate();
        return *this;
    }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    TextBox& Glyph(std::wstring_view value) {
        glyph_ = value;
        RelayoutParent();
        return *this;
    }
    bool ReadOnly() const noexcept { return read_only_; }
    TextBox& ReadOnly(bool value) {
        read_only_ = value;
        return *this;
    }
    bool Password() const noexcept { return password_; }
    TextBox& Password(bool value);
    bool Multiline() const noexcept { return multiline_; }
    TextBox& Multiline(bool value);
    // 0 = 不限制。插入/粘贴/IME 提交超出则截断。
    size_t MaxLength() const noexcept { return max_length_; }
    TextBox& MaxLength(size_t n);
    // 输入掩码：`0` 数字、`A` 字母、`*` 任意可见字符，其余为字面量（如 `000-0000`）。
    const std::wstring& Mask() const noexcept { return input_mask_; }
    TextBox& Mask(std::wstring_view pattern);
    // 占位符上浮成 Caption 标签（单行；与 FormField 外置标签互斥选用）。
    bool FloatingLabel() const noexcept { return floating_label_; }
    TextBox& FloatingLabel(bool value = true);

    TextBox& OnTextChanged(std::function<void(std::wstring_view)> handler) {
        text_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindTextChanged(std::function<void(std::wstring_view)> handler) {
        return text_changed_.Connect(std::move(handler));
    }
    TextBox& OnSubmit(std::function<void()> handler) {
        submit_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSubmit(std::function<void()> handler) { return submit_.Connect(std::move(handler)); }
    TextBox& BindText(Property<std::wstring>& p);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Edit;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternValue; }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? placeholder_ : accessible_name_;
    }
    std::wstring AutomationValue() const override { return text_; }
    bool AutomationSetValue(std::wstring_view value) override {
        if (!enabled_ || read_only_) return false;
        Text(value);
        return true;
    }
    bool AutomationIsReadOnly() const noexcept override { return read_only_; }
    bool AutomationIsPassword() const noexcept override { return password_; }
    void OnFocusChanged(bool focused) override;
    bool OnKey(uint32_t vk) override;
    bool OnChar(wchar_t ch) override;
    bool ImeInline() const noexcept override { return !read_only_; }
    bool ImeComposing() const noexcept override { return !ime_comp_.empty(); }
    void OnImeCompose(std::wstring_view text, size_t cursor, std::string_view attributes) override;
    void OnImeCommit(std::wstring_view text) override;
    void OnImeEnd() override;
    bool ImeCaret(Point& window_dip, float& height_dip) const override;
    CursorShape CursorAt(Point local) const override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseDoubleClick(Point local) override;
    bool PrefersDragOverPan() const noexcept override { return true; }
    bool AcceptsTextDrop() const noexcept override;
    void OnTextDrop(std::wstring_view text, Point window_dip) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    void InsertText(const wchar_t* begin, size_t count);
    void DeleteSelection();
    size_t SelectionStart() const noexcept;
    size_t SelectionEnd() const noexcept;
    bool HasSelection() const noexcept { return caret_ != anchor_; }
    void SetCaret(size_t index, bool extend = false, bool scroll_to_caret = true);
    float CaretX(size_t index) const;
    float CaretY(size_t index) const;
    float TextAdvance(std::wstring_view text) const;
    float LineHeight() const;
    size_t HardLineCount() const noexcept;
    float ContentWidth() const;
    size_t LineStart(size_t index) const;
    size_t LineEnd(size_t index) const;
    size_t HitIndex(Point local) const;
    void NotifyImeCaret();
    void ClearCompose();
    float VisualCaretX() const;
    void DrawComposition(Painter& painter, const Theme& theme, float x, float text_y, float text_h,
                         float band_y, float band_h) const;
    virtual bool PaintChrome() const noexcept { return true; }
    virtual TextRole ContentRole() const noexcept { return TextRole::Body; }
    virtual float PadLeft() const;
    float PadTop() const;
    // 右侧内容留白；内嵌尾部控件（NumberBox spin 区）的子类覆写收窄文本区。
    virtual float PadRight() const;
    void ScrollCaretIntoView();
    void CopyOrCut(bool cut);
    void Paste();
    void RefreshMask();
    const std::wstring& VisibleText() const noexcept;
    enum class EditOp : uint8_t { None, Type, Erase, Other };
    void PushUndo(EditOp op = EditOp::Other);
    bool Undo();
    bool Redo();
    void NotifyChanged();
    size_t WordLeft(size_t index) const;
    size_t WordRight(size_t index) const;
    void SelectWordAt(size_t index);
    void SelectLineAt(size_t index);
    void EnsureEditMenu();
    void MoveSelectionTo(size_t dest);
    void InsertMasked(wchar_t ch);
    void ApplyClick(Point local, uint8_t count);

    struct Snapshot {
        std::wstring text;
        size_t caret = 0;
        size_t anchor = 0;
    };

    std::wstring text_;
    std::wstring mask_;
    std::wstring input_mask_;
    std::wstring placeholder_;
    std::wstring glyph_;
    std::wstring ime_comp_;
    std::string ime_attr_;
    size_t ime_cursor_ = 0;
    wchar_t pending_high_surrogate_ = 0;
    size_t caret_ = 0;
    size_t anchor_ = 0;
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    bool read_only_ = false;
    bool password_ = false;
    bool multiline_ = false;
    bool floating_label_ = false;
    bool caret_on_ = true;
    float blink_t_ = 0.0f;
    float float_t_ = 0.0f;
    size_t max_length_ = 0;
    uint8_t click_count_ = 0;
    unsigned long last_click_tick_ = 0;
    Point last_click_local_{};
    bool selection_drag_ = false;
    bool selection_dragging_ = false;
    bool drop_inserted_ = false;
    bool ole_source_ = false;
    Point drag_origin_{};
    size_t drop_preview_ = static_cast<size_t>(-1);
    EditOp last_op_ = EditOp::None;
    unsigned long last_op_tick_ = 0;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    Signal<std::wstring_view> text_changed_;
    Signal<> submit_;
    ScopedConnection text_prop_;
    ScopedConnection text_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
