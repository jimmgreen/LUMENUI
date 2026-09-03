// lumen/NavigationView.h — 支持任意层级的导航侧栏 + 内容区。
// Events: OnSelectionChanged / BindSelectionChanged / OnItemInvoked / BindItemInvoked / OnExpandedChanged / BindExpandedChanged / OnSearch / BindSearch
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "InfoBadge.h"
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

class AutoSuggestBox;
class Breadcrumb;

enum class NavigationItemType { Item, Header, Separator };
enum class NavigationDisplayMode { Auto, Expanded, Compact };

struct NavigationItem {
    std::wstring id;
    std::wstring text;
    std::wstring glyph;
    NavigationItemType type = NavigationItemType::Item;
    bool enabled = true;
    std::wstring parent_id;
    bool expanded = false;
    bool action = false;
    InfoBadgeData badge{};
};

class NavigationView : public PanelOf<NavigationView> {
public:
    NavigationView();
    ~NavigationView() override;

    NavigationView& Items(std::vector<NavigationItem> items);
    NavigationView& FooterItems(std::vector<NavigationItem> items);
    const std::wstring& SelectedId() const noexcept;
    NavigationView& SelectedId(std::wstring_view id);
    NavigationView& OnSelectionChanged(std::function<void(std::wstring_view)> handler);
    Connection BindSelectionChanged(std::function<void(std::wstring_view)> handler);
    NavigationView& OnItemInvoked(std::function<void(std::wstring_view)> handler);
    Connection BindItemInvoked(std::function<void(std::wstring_view)> handler);
    NavigationView& BindPages(class PageHost& host);
    StackPanel& Page(std::wstring_view id);
    NavigationView& Navigate(std::wstring_view id);
    NavigationView& DisplayMode(NavigationDisplayMode value);
    NavigationDisplayMode DisplayMode() const noexcept;
    NavigationView& PaneLength(float value);
    NavigationView& CompactLength(float value);
    NavigationView& Height(float value);
    NavigationView& ItemExpanded(std::wstring_view id, bool expanded);
    bool ItemExpanded(std::wstring_view id) const;
    NavigationView& ItemBadge(std::wstring_view id, InfoBadgeData badge);
    InfoBadgeData ItemBadge(std::wstring_view id) const;
    NavigationView& RevealId(std::wstring_view id);
    NavigationView& OnExpandedChanged(std::function<void(std::wstring_view, bool)> handler);
    Connection BindExpandedChanged(std::function<void(std::wstring_view, bool)> handler);
    StackPanel& Content();

    // 窗格内搜索：过滤树（匹配项及其祖先保持可见），建议列表点选即跳转。
    NavigationView& SearchEnabled(bool on = true);
    bool SearchEnabled() const noexcept;
    NavigationView& SearchPlaceholder(std::wstring_view text);
    NavigationView& SearchQuery(std::wstring_view query);
    const std::wstring& SearchQuery() const noexcept;
    NavigationView& OnSearch(std::function<void(std::wstring_view)> handler);
    Connection BindSearch(std::function<void(std::wstring_view)> handler);
    AutoSuggestBox* SearchBox() noexcept;
    const AutoSuggestBox* SearchBox() const noexcept;

    // 内容区顶栏面包屑：随选中项路径更新；点祖先即跳转。也可绑外部 Breadcrumb。
    NavigationView& ShowBreadcrumb(bool on = true);
    bool BreadcrumbVisible() const noexcept;
    std::vector<std::wstring> PathIds() const;
    std::vector<std::wstring> PathTitles() const;
    NavigationView& BindBreadcrumb(Breadcrumb& crumb);

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool ClipChildren() const noexcept override { return true; }
    Rect ChildrenClipBounds() const noexcept override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override;
    uint32_t AutomationPatterns() const noexcept override;
    int AutomationSelectedIndex() const noexcept override;
    int AutomationItemCount() const noexcept override;
    bool AutomationSelectIndex(int index) override;
    std::wstring AutomationItemName(int index) const override;
    bool OnKey(uint32_t vk) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    void OnFocusChanged(bool focused) override;
    CursorShape CursorAt(Point local) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lumen
