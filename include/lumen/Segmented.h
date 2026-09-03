// lumen/Segmented.h — 分段选择器（容器内一组互斥选项）。
// Events: OnSelectionChanged / BindSelectionChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Animate.h"
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class Segmented : public ControlOf<Segmented> {
public:
    int AddItem(std::wstring_view text);
    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    Segmented& SelectedIndex(ptrdiff_t index);
    Segmented& OnSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        return changed_.Connect(std::move(handler));
    }
    Segmented& BindSelectedIndex(Property<int>& p);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    float ItemWidth(size_t index, const Theme& theme);
    int ItemAt(Point local, const Theme& theme);
    void ItemSlot(size_t index, float& x, float& w, const Theme& theme);
    void SnapThumb();
    void ApplyThumb(float t);

    std::vector<std::wstring> items_;
    ptrdiff_t selected_ = 0;
    int hover_item_ = -1;
    float thumb_x_ = 4.0f;
    float thumb_w_ = 0.0f;
    float thumb_from_x_ = 4.0f;
    float thumb_from_w_ = 0.0f;
    float thumb_to_x_ = 4.0f;
    float thumb_to_w_ = 0.0f;
    Tween slide_{};
    bool thumb_ready_ = false;
    Signal<ptrdiff_t, ptrdiff_t> changed_;
    ScopedConnection index_prop_;
    ScopedConnection index_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
