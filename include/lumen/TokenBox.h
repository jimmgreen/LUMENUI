// lumen/TokenBox.h — 标签输入：键入后提交为 Chip，可换行。
// Events: OnTokensChanged / BindTokensChanged
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class Chip;

class TokenBox : public PanelOf<TokenBox> {
public:
    TokenBox();

    const std::vector<std::wstring>& Tokens() const noexcept { return tokens_; }
    TokenBox& Tokens(std::vector<std::wstring> value);
    bool AddToken(std::wstring_view text);
    bool RemoveToken(std::wstring_view text);
    bool RemoveTokenAt(size_t index);
    bool RemoveLast();
    void ClearTokens();

    TokenBox& Placeholder(std::wstring_view value);
    const std::wstring& Placeholder() const noexcept { return placeholder_; }
    TokenBox& AllowDuplicates(bool value) {
        allow_duplicates_ = value;
        return *this;
    }
    bool AllowDuplicates() const noexcept { return allow_duplicates_; }
    TokenBox& MaxTokens(size_t value) {
        max_tokens_ = value;
        return *this;
    }
    size_t MaxTokens() const noexcept { return max_tokens_; }

    const std::wstring& Draft() const noexcept;
    TokenBox& Draft(std::wstring_view text);
    bool CommitDraft();

    TokenBox& OnTokensChanged(std::function<void()> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindTokensChanged(std::function<void()> handler) {
        return changed_.Connect(std::move(handler));
    }
    TokenBox& BindTokens(Property<std::vector<std::wstring>>& p);

    bool Enabled() const noexcept { return Control::Enabled(); }
    TokenBox& Enabled(bool value);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool ClipChildren() const noexcept override { return true; }
    void OnMouseDown(Point local, uint32_t buttons) override;

private:
    class Field;
    void InsertChip(std::wstring_view text);
    void CompactHidden();
    void SyncPlaceholder();
    void SplitDraft();
    void NotifyChanged();
    bool AtLimit() const noexcept;
    Chip* ChipAt(size_t token_index);
    Field* field_ = nullptr;
    std::vector<std::wstring> tokens_;
    std::wstring placeholder_{L"Add"};
    bool allow_duplicates_ = false;
    size_t max_tokens_ = 0;
    Signal<> changed_;
    ScopedConnection tokens_prop_;
    ScopedConnection tokens_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
