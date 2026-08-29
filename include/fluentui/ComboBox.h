// fluentui/ComboBox.h — 下拉选择框。
#pragma once
#include "Control.h"
#include <functional>
#include <string>
#include <vector>

namespace fui {

class ComboBox : public Control {
public:
    void AddItem(std::wstring_view text);
    void ClearItems();
    size_t Count() const noexcept { return items_.size(); }
    const std::wstring& ItemAt(size_t index) const { return items_[index]; }

    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    void SetSelectedIndex(ptrdiff_t index);   // -1 表示无选中
    std::wstring SelectedText() const;

    void OnSelectionChanged(std::function<void()> handler) { changed_ = std::move(handler); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnFocusChanged(bool focused) override;

    void RelayoutParent();
    void OpenPopup();

    std::vector<std::wstring> items_;
    ptrdiff_t selected_ = -1;
    bool dropdown_open_ = false;   // 弹出期间绘制按下态
    std::function<void()> changed_;
};

} // namespace fui
