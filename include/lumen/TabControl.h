// lumen/TabControl.h — 标签页：下划线滑动切换；AddTab 返回每页内容容器。
// Events: OnSelectionChanged / BindSelectionChanged / OnTabClosing / OnTabClosed / BindTabClosed / OnReordered / BindReordered
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Animate.h"
#include "InfoBadge.h"
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class StackPanel;

struct TabItem {
    std::wstring id;
    std::wstring title;
    std::wstring glyph;
    bool closable = false;
    InfoBadgeData badge{};
};

class TabControl : public PanelOf<TabControl> {
public:
    // 新增一页并返回其内容容器（纵向 StackPanel）。控件由 TabControl 持有。
    StackPanel& AddTab(std::wstring_view title);
    StackPanel& AddTab(TabItem item);

    size_t TabCount() const noexcept { return items_.size(); }
    const TabItem& Tab(size_t index) const;

    int SelectedIndex() const noexcept { return selected_; }
    TabControl& SelectedIndex(int index);
    const std::wstring& SelectedId() const noexcept;
    TabControl& SelectedId(std::wstring_view id);
    bool CloseTab(std::wstring_view id);
    TabControl& TabBadge(std::wstring_view id, InfoBadgeData badge);
    InfoBadgeData TabBadge(std::wstring_view id) const;
    TabControl& OnSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        return changed_.Connect(std::move(handler));
    }
    TabControl& BindSelectedIndex(Property<int>& p);
    TabControl& OnTabClosing(std::function<bool(std::wstring_view)> handler) {
        closing_ = std::move(handler);
        return *this;
    }
    TabControl& OnTabClosed(std::function<void(std::wstring_view)> handler) {
        closed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindTabClosed(std::function<void(std::wstring_view)> handler) {
        return closed_.Connect(std::move(handler));
    }

    // 拖动排序：松手按落点插入。默认开。
    TabControl& CanReorder(bool on) {
        can_reorder_ = on;
        return *this;
    }
    bool CanReorder() const noexcept { return can_reorder_; }
    TabControl& MoveTab(size_t from, size_t to);
    TabControl& OnReordered(std::function<void(size_t from, size_t to)> handler) {
        reordered_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindReordered(std::function<void(size_t from, size_t to)> handler) {
        return reordered_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Tab;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternSelection; }
    int AutomationSelectedIndex() const noexcept override { return selected_; }
    int AutomationItemCount() const noexcept override { return static_cast<int>(items_.size()); }
    bool AutomationSelectIndex(int index) override {
        if (index < 0 || static_cast<size_t>(index) >= items_.size()) return false;
        SelectedIndex(index);
        return true;
    }
    std::wstring AutomationItemName(int index) const override {
        if (index < 0 || static_cast<size_t>(index) >= items_.size()) return {};
        return items_[static_cast<size_t>(index)].title;
    }
    bool OnKey(uint32_t vk) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    bool OnHWheel(float delta) override;
    bool CapturesOverlay(Point p) const override;
    bool PrefersDragOverPan() const noexcept override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    float TabWidth(size_t index, const Theme& theme) const;
    int TabAt(Point local, const Theme& theme);
    bool CloseAt(Point local, size_t index, const Theme& theme);
    bool OverflowAt(Point local) const;
    void ShowOnlySelected();
    void IndicatorSlot(size_t index, float& x, float& w);
    void SnapIndicator();
    void ApplyIndicator(float t);
    bool StripOverflows() const;
    float StripViewport() const;
    float StripContent() const;
    float MaxScroll() const;
    void ClampScroll();
    void EnsureSelectedVisible();
    int DropSlotAt(Point local, const Theme& theme);
    void OpenOverflow();
    void EndDrag();

    std::wstring MakeId(std::wstring_view preferred) const;
    std::vector<TabItem> items_;
    int selected_ = 0;
    int hover_tab_ = -1;
    int press_tab_ = -1;
    int drag_tab_ = -1;
    int drop_slot_ = -1;
    bool hover_overflow_ = false;
    bool can_reorder_ = true;
    float scroll_x_ = 0.0f;
    float last_pointer_y_ = -1.0f;
    Point press_local_{};
    float indicator_x_ = 0.0f;
    float indicator_w_ = 0.0f;
    float indicator_from_x_ = 0.0f;
    float indicator_from_w_ = 0.0f;
    float indicator_to_x_ = 0.0f;
    float indicator_to_w_ = 0.0f;
    Tween slide_{};
    bool indicator_ready_ = false;
    Signal<ptrdiff_t, ptrdiff_t> changed_;
    std::function<bool(std::wstring_view)> closing_;
    Signal<std::wstring_view> closed_;
    Signal<size_t, size_t> reordered_;
    ScopedConnection index_prop_;
    ScopedConnection index_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
