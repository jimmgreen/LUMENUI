#include "lumen/AutoSuggestBox.h"
#include "lumen/Menu.h"
#include "../core/menu_window.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>

namespace lumen {

std::vector<std::wstring> AutoSuggestBox::QuerySuggestions() const {
    std::vector<std::wstring> out;
    if (!provider_) return out;
    out = provider_(text_);
    if (out.size() > max_suggestions_) out.resize(max_suggestions_);
    return out;
}

AutoSuggestBox& AutoSuggestBox::Bind(ItemsModel& model) {
    owned_model_.reset();
    model_ = &model;
    Suggestions([this](std::wstring_view query) {
        std::vector<std::wstring> out;
        if (!model_) return out;
        ItemRow row;
        const size_t n = model_->Count();
        for (size_t i = 0; i < n; ++i) {
            model_->Get(i, row);
            if (!query.empty() && row.text.find(query) == std::wstring::npos) continue;
            out.push_back(row.text);
            if (out.size() >= max_suggestions_) break;
        }
        return out;
    });
    model_detached_ = ScopedConnection(model.OnDetached([this] {
        model_ = nullptr;
        owned_model_.reset();
        Suggestions({});
    }));
    return *this;
}

AutoSuggestBox& AutoSuggestBox::Bind(std::shared_ptr<ItemsModel> model) {
    if (!model) return *this;
    ItemsModel& ref = *model;
    Bind(ref);
    owned_model_ = std::move(model);
    return *this;
}

void AutoSuggestBox::ShowSuggestions() {
    std::vector<std::wstring> items = QuerySuggestions();
    // 无窗口（测试/未入树）时不开弹层；菜单是模态循环，已开时重入会叠出双层菜单。
    if (items.empty() || !window_ || suggestions_open_) return;

    std::vector<MenuItem> menu_items;
    auto rebuild = [&] {
        items = QuerySuggestions();
        menu_items.clear();
        menu_items.reserve(items.size());
        for (const std::wstring& item : items) {
            MenuItem entry;
            entry.text = item;
            menu_items.push_back(std::move(entry));
        }
    };
    rebuild();
    POINT px{static_cast<LONG>(absolute_.x * WindowImpl::ScaleOf(window_)),
             static_cast<LONG>((absolute_.Bottom() + 4.0f) * WindowImpl::ScaleOf(window_))};
    ClientToScreen(WindowImpl::HwndOf(window_), &px);
    suggestions_open_ = true;
    auto on_char = [&](wchar_t ch) -> bool {
        if (read_only_) return false;
        if (ch == 0x08) {
            TextBox::OnKey(VK_BACK);
            rebuild();
            Invalidate();
            return true;
        }
        const bool handled = TextBox::OnChar(ch);
        if (!handled) return false;
        rebuild();
        Invalidate();
        return true;
    };
    const int result = MenuWindow::Show(WindowImpl::HwndOf(window_), menu_items, px,
                                        WindowImpl::ThemeOf(window_), WindowImpl::ScaleOf(window_),
                                        absolute_.w, on_char);
    suggestions_open_ = false;
    if (result >= 0 && result < static_cast<int>(items.size())) {
        const std::wstring& picked = items[static_cast<size_t>(result)];
        Text(picked);   // 回填不入撤销栈、不重复触发 OnTextChanged
        chosen_.Emit(picked);
    } else {
        Focus();
    }
    Invalidate();
}

bool AutoSuggestBox::OnChar(wchar_t ch) {
    if (read_only_) return false;
    const bool handled = TextBox::OnChar(ch);
    if (handled) ShowSuggestions();
    return handled;
}

void AutoSuggestBox::OnImeCommit(std::wstring_view text) {
    TextBox::OnImeCommit(text);
    if (!text.empty() && !read_only_) ShowSuggestions();
}

bool AutoSuggestBox::OnKey(uint32_t vk) {
    if (read_only_) return TextBox::OnKey(vk);
    if (vk == VK_DOWN) {
        ShowSuggestions();
        return true;
    }
    return TextBox::OnKey(vk);
}

} // namespace lumen
