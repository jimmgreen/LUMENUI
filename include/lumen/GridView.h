// lumen/GridView.h — 虚拟化图标网格：按列折行，只画可见行。
// Events: BindSelectionChanged / OnSelectionChanged / OnActivate / BindActivate
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "ItemsModel.h"
#include "Signal.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>

namespace lumen {

class GridView : public ControlOf<GridView> {
public:
    GridView& ItemCount(size_t count);
    size_t ItemCount() const noexcept { return item_count_; }
    GridView& ItemSize(Size value);
    Size ItemSize() const noexcept { return item_size_; }
    GridView& ItemGap(float value) {
        item_gap_ = std::max(0.0f, value);
        RelayoutParent();
        return *this;
    }

    GridView& ItemText(std::function<void(size_t, std::wstring&)> provider) {
        item_text_ = std::move(provider);
        Invalidate();
        return *this;
    }
    GridView& ItemGlyph(std::function<void(size_t, std::wstring&)> provider) {
        item_glyph_ = std::move(provider);
        Invalidate();
        return *this;
    }

    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    ptrdiff_t SelectedDataIndex() const noexcept { return selected_; }
    GridView& SelectedIndex(ptrdiff_t index);
    GridView& Bind(ItemsModel& model);
    GridView& Bind(std::shared_ptr<ItemsModel> model);
    Connection BindSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        return selection_changed_.Connect(std::move(handler));
    }
    GridView& OnSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        selection_changed_.Subscribe(std::move(handler));
        return *this;
    }
    GridView& OnActivate(std::function<void(size_t)> handler) {
        activate_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindActivate(std::function<void(size_t)> handler) {
        return activate_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseDoubleClick(Point local) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    bool CapturesOverlay(Point p) const override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    void BeginEnter();
    float ItemEnter(size_t visible_index) const noexcept;
    int Columns() const;
    float ContentHeight() const;
    float MaxScroll() const;
    ptrdiff_t ItemAt(Point local) const;
    void ClampScroll();
    void ScrollTo(size_t index);
    Rect VerticalTrack() const noexcept;
    ScrollThumb Thumb(float expand) const noexcept;

    size_t item_count_ = 0;
    Size item_size_{88.0f, 92.0f};
    float item_gap_ = 8.0f;
    ptrdiff_t selected_ = -1;
    ptrdiff_t hover_ = -1;
    float scroll_offset_ = 0.0f;
    float target_offset_ = 0.0f;
    float expand_progress_ = 0.0f;
    bool dragging_ = false;
    float drag_grab_ = 0.0f;
    std::wstring draw_text_;
    std::wstring draw_glyph_;
    std::function<void(size_t, std::wstring&)> item_text_;
    std::function<void(size_t, std::wstring&)> item_glyph_;
    Signal<ptrdiff_t, ptrdiff_t> selection_changed_;
    Signal<size_t> activate_;
    bool enter_playing_ = false;
    float enter_elapsed_ = 10.0f;
    ItemsModel* model_ = nullptr;
    std::shared_ptr<ItemsModel> owned_model_;
    ItemRow model_row_{};
    ptrdiff_t model_cache_ = -1;
    ScopedConnection model_inserted_;
    ScopedConnection model_removed_;
    ScopedConnection model_changed_;
    ScopedConnection model_reset_;
    ScopedConnection model_detached_;
};
} // namespace lumen
