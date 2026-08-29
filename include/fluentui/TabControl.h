// fluentui/TabControl.h — 标签页：AddTab 返回每页的内容容器（纵向堆叠）。
#pragma once
#include "Panel.h"
#include <functional>
#include <string>
#include <vector>

namespace fui {

class StackPanel;

class TabControl : public Panel {
public:
    // 新增一页并返回其内容容器（纵向 StackPanel）。控件由 TabControl 持有。
    StackPanel& AddTab(std::wstring_view title);

    int SelectedIndex() const noexcept { return selected_; }
    void SetSelectedIndex(int index);
    void OnSelectionChanged(std::function<void()> handler) { changed_ = std::move(handler); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;

    void RelayoutParent();
    float TabWidth(size_t index, const Theme& theme);
    int TabAt(Point local, const Theme& theme);
    void ShowOnlySelected();

    std::vector<std::wstring> titles_;
    int selected_ = 0;
    int hover_tab_ = -1;
    std::function<void()> changed_;
};

} // namespace fui
