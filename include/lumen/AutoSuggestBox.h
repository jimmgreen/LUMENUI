// lumen/AutoSuggestBox.h — 建议输入框：输入过滤候选项，弹层选择后回填。
// Events: OnSuggestionChosen / BindSuggestionChosen
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "ItemsModel.h"
#include "TextBox.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

class AutoSuggestBox : public TextBox {
public:
    AutoSuggestBox() = default;
    explicit AutoSuggestBox(std::wstring_view text) : TextBox(text) {}

    // 候选数据源：按当前输入返回建议（控件不再过滤/去重，顺序即展示顺序）。
    AutoSuggestBox& Suggestions(
        std::function<std::vector<std::wstring>(std::wstring_view query)> provider) {
        provider_ = std::move(provider);
        return *this;
    }
    AutoSuggestBox& MaxSuggestions(size_t value) {
        max_suggestions_ = std::max<size_t>(1, value);
        return *this;
    }
    // 弹层内选中后回填文本并回调。
    AutoSuggestBox& OnSuggestionChosen(std::function<void(std::wstring_view)> handler) {
        chosen_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSuggestionChosen(std::function<void(std::wstring_view)> handler) {
        return chosen_.Connect(std::move(handler));
    }
    AutoSuggestBox& Bind(ItemsModel& model);
    AutoSuggestBox& Bind(std::shared_ptr<ItemsModel> model);
    bool SuggestionsOpen() const noexcept { return suggestions_open_; }

    // 以当前文本查询候选（过滤 + 截断到 MaxSuggestions）。公开便于测试与自定义弹层。
    std::vector<std::wstring> QuerySuggestions() const;

    AutoSuggestBox& FocusInput() { Focus(); return *this; }

    AutoSuggestBox& Placeholder(std::wstring_view value) {
        TextBox::Placeholder(value);
        return *this;
    }
    AutoSuggestBox& Glyph(std::wstring_view value) {
        TextBox::Glyph(value);
        return *this;
    }

protected:
    friend class WindowImpl;
    bool OnChar(wchar_t ch) override;
    void OnImeCommit(std::wstring_view text) override;
    bool OnKey(uint32_t vk) override;

private:
    void ShowSuggestions();

    std::function<std::vector<std::wstring>(std::wstring_view)> provider_;
    Signal<std::wstring_view> chosen_;
    size_t max_suggestions_ = 8;
    bool suggestions_open_ = false;
    ItemsModel* model_ = nullptr;
    std::shared_ptr<ItemsModel> owned_model_;
    ScopedConnection model_detached_;
};

} // namespace lumen
