// lumen/ComboBox.h — 下拉选择框：虚拟化列表、分组、多选 Chip、键入跳转。
// Events: OnSelectionChanged / BindSelectionChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "ItemsModel.h"
#include "Signal.h"
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

struct ComboGroup {
    std::wstring id;
    std::wstring title;
    size_t item_count = 0;
};

class ComboBox : public PanelOf<ComboBox> {
public:
    ComboBox();
    ~ComboBox() override;
    ComboBox& AddItem(std::wstring_view text);
    // 批量加入候选并返回自身：AddItems({L"A", L"B"}).SelectedIndex(0)。
    ComboBox& AddItems(std::initializer_list<std::wstring_view> texts) {
        for (std::wstring_view text : texts) items_.emplace_back(text);
        RelayoutParent();
        return *this;
    }
    ComboBox& ClearItems();
    ComboBox& Items(std::vector<std::wstring> items);
    ComboBox& MaxDropDownRows(size_t value);
    size_t MaxDropDownRows() const noexcept { return max_dropdown_rows_; }
    size_t Count() const noexcept { return items_.size(); }
    const std::wstring& ItemAt(size_t index) const {
        if (index >= items_.size()) {
            Control::DebugTrap(L"LUMEN_CHECK: ComboBox::ItemAt index out of range");
            static const std::wstring empty;
            return empty;
        }
        return items_[index];
    }

    ComboBox& Groups(std::vector<ComboGroup> groups);
    const std::vector<ComboGroup>& Groups() const noexcept { return groups_; }

    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    ptrdiff_t SelectedDataIndex() const noexcept { return selected_; }
    ComboBox& SelectedIndex(ptrdiff_t index);   // -1 表示无选中；多选时同时保证该项在集合内
    std::wstring SelectedText() const;
    ComboBox& Placeholder(std::wstring_view value) {
        placeholder_ = value;
        Invalidate();
        return *this;
    }
    bool Editable() const noexcept { return editable_; }
    ComboBox& Editable(bool value);

    // 多选：下拉勾选切换且不关闭；锚点以可关闭 Chip 展示已选项。
    // 开启时关闭 Editable（键入走过滤与 Chip 互斥）。
    ComboBox& MultiSelect(bool on);
    bool MultiSelect() const noexcept { return multi_; }
    bool IsSelected(size_t index) const;
    std::vector<size_t> SelectedIndices() const;
    ComboBox& SelectedIndices(std::vector<ptrdiff_t> indices);
    ComboBox& ClearSelection();
    size_t SelectionCount() const noexcept {
        return multi_ ? selected_set_.size() : (selected_ >= 0 ? size_t{1} : size_t{0});
    }

    ComboBox& OnSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        return changed_.Connect(std::move(handler));
    }
    ComboBox& Bind(ItemsModel& model);
    ComboBox& Bind(std::shared_ptr<ItemsModel> model);
    ComboBox& BindSelectedIndex(Property<int>& p);

    bool Enabled() const noexcept { return Control::Enabled(); }
    ComboBox& Enabled(bool value);

protected:
    friend class WindowImpl;
    class DropdownPopup;
    friend class DropdownPopup;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool ClipChildren() const noexcept override { return true; }
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::ComboBox;
    }
    uint32_t AutomationPatterns() const noexcept override {
        return kPatternValue | kPatternSelection | kPatternExpand;
    }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? placeholder_ : accessible_name_;
    }
    std::wstring AutomationValue() const override { return SelectedText(); }
    bool AutomationSetValue(std::wstring_view value) override {
        if (!enabled_) return false;
        if (editable_) {
            edit_text_ = std::wstring(value);
            Invalidate();
            return true;
        }
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i] == value) {
                SelectedIndex(static_cast<ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }
    bool AutomationIsReadOnly() const noexcept override { return !editable_; }
    int AutomationExpandState() const noexcept override { return dropdown_open_ ? 1 : 0; }
    bool AutomationExpand() override {
        if (!enabled_) return false;
        if (!dropdown_open_) OpenPopup(false);
        return true;
    }
    bool AutomationCollapse() override { return true; }
    int AutomationSelectedIndex() const noexcept override { return static_cast<int>(selected_); }
    int AutomationItemCount() const noexcept override { return static_cast<int>(items_.size()); }
    bool AutomationSelectIndex(int index) override {
        if (index < -1 || index >= static_cast<int>(items_.size())) return false;
        SelectedIndex(index);
        return true;
    }
    std::wstring AutomationItemName(int index) const override {
        if (index < 0 || static_cast<size_t>(index) >= items_.size()) return {};
        return items_[static_cast<size_t>(index)];
    }
    bool OnKey(uint32_t vk) override;
    bool OnChar(wchar_t ch) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    // from_typing 时才用 edit_text_ 过滤候选项；点击/键盘打开必须展示全部条目，
    // 否则选中后的 edit_text_ 会把下拉过滤得只剩当前项。
    void OpenPopup(bool from_typing = false);
    void TypeJump(wchar_t ch);
    void ToggleItem(size_t data);
    void FilterSelection();
    void SyncChips();
    bool ItemPicked(size_t data) const noexcept;

    std::vector<std::wstring> items_;
    std::vector<ComboGroup> groups_;
    std::wstring placeholder_;
    std::wstring edit_text_;
    std::wstring jump_;
    ptrdiff_t selected_ = -1;
    std::vector<ptrdiff_t> selected_set_;
    bool dropdown_open_ = false;
    bool editable_ = false;
    bool multi_ = false;
    float glow_t_ = 0.0f;
    float chevron_t_ = 0.0f;
    float jump_age_ = 0.0f;
    Signal<ptrdiff_t, ptrdiff_t> changed_;
    std::unique_ptr<DropdownPopup> popup_;
    size_t max_dropdown_rows_ = 8;
    ItemsModel* model_ = nullptr;
    std::shared_ptr<ItemsModel> owned_model_;
    ScopedConnection model_reset_;
    ScopedConnection model_inserted_;
    ScopedConnection model_removed_;
    ScopedConnection model_changed_;
    ScopedConnection model_detached_;
    ScopedConnection index_prop_;
    ScopedConnection index_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
