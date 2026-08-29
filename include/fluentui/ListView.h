// fluentui/ListView.h — 虚拟化单选列表：行按需绘制，十万行也只画可见行。
#pragma once
#include "Control.h"
#include <functional>
#include <string>

namespace fui {

class ListView : public Control {
public:
    void SetItemCount(size_t count) { item_count_ = count; ClampScroll(); RelayoutParent(); }
    size_t ItemCount() const noexcept { return item_count_; }

    // 数据提供回调：返回行文本（与可选字形）。行不存在返回空串。
    void ItemText(std::function<std::wstring(size_t)> provider) { item_text_ = std::move(provider); }
    void ItemGlyph(std::function<std::wstring(size_t)> provider) { item_glyph_ = std::move(provider); }

    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    void SetSelectedIndex(ptrdiff_t index);   // -1 = 无选中；自动滚动到可见
    void ScrollTo(size_t index);

    void OnSelectionChanged(std::function<void()> handler) { selection_changed_ = std::move(handler); }
    void OnActivate(std::function<void()> handler) { activate_ = std::move(handler); }   // 双击/回车

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseDoubleClick(Point local) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnWheel(float delta) override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    float RowHeight(const Theme& theme) const noexcept { return theme.list_row_height; }
    ptrdiff_t RowAt(Point local) const;
    void ClampScroll();
    void MoveSelection(ptrdiff_t delta);
    float MaxScroll() const;

    size_t item_count_ = 0;
    ptrdiff_t selected_ = -1;
    float scroll_offset_ = 0.0f;     // 当前滚动（DIP，平滑插值）
    float target_offset_ = 0.0f;     // 目标滚动
    float theme_row_height_ = 30.0f;
    ptrdiff_t hover_row_ = -1;
    ptrdiff_t keyboard_anchor_ = -1; // Shift 选区起点
    std::function<std::wstring(size_t)> item_text_;
    std::function<std::wstring(size_t)> item_glyph_;
    std::function<void()> selection_changed_;
    std::function<void()> activate_;
};

} // namespace fui
