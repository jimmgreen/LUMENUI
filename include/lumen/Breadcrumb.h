// lumen/Breadcrumb.h — 面包屑：层级路径导航条，段间 chevron 分隔。
// Events: OnNavigate / BindNavigate
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace lumen {

class Breadcrumb : public ControlOf<Breadcrumb> {
public:
    Breadcrumb() = default;
    explicit Breadcrumb(std::initializer_list<std::wstring_view> crumbs) {
        for (std::wstring_view crumb : crumbs) items_.emplace_back(crumb);
    }

    Breadcrumb& AddItem(std::wstring_view text) {
        items_.emplace_back(text);
        layout_cache_dirty_ = true;
        RelayoutParent();
        return *this;
    }
    Breadcrumb& ClearItems() {
        items_.clear();
        layout_cache_dirty_ = true;
        RelayoutParent();
        return *this;
    }
    Breadcrumb& Items(std::vector<std::wstring> items) {
        items_ = std::move(items);
        layout_cache_dirty_ = true;
        selected_ = items_.empty() ? -1 : static_cast<ptrdiff_t>(items_.size()) - 1;
        RelayoutParent();
        return *this;
    }
    size_t Count() const noexcept { return items_.size(); }

    // 编程选中（不触发 OnNavigate；-1 = 无选中）。
    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    Breadcrumb& SelectedIndex(ptrdiff_t index);
    Breadcrumb& OnNavigate(std::function<void(size_t index)> handler) {
        navigate_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindNavigate(std::function<void(size_t index)> handler) {
        return navigate_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return !items_.empty(); }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    CursorShape CursorAt(Point local) const override;

private:
    void RefreshCache() const;   // 段几何缓存；绘制只读
    void Navigate(size_t index);

    std::vector<std::wstring> items_;
    // 段几何缓存：Measure/文本变化时在输入路径重建，绘制路径零分配。
    mutable std::vector<float> segment_x_;
    mutable std::vector<float> segment_w_;
    mutable bool layout_cache_dirty_ = true;
    ptrdiff_t selected_ = -1;
    ptrdiff_t hover_ = -1;
    Signal<size_t> navigate_;
};

} // namespace lumen
