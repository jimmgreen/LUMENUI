// lumen/LogView.h — 等宽虚拟化日志：贴底跟随、按级亮度、Ctrl+C 复制选中行。
// Events: 无（本头无订阅事件）
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Log.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class LogView : public ControlOf<LogView> {
public:
    LogView& ItemCount(size_t count);
    size_t ItemCount() const noexcept { return item_count_; }
    LogView& LineText(std::function<void(size_t, std::wstring&)> provider) {
        line_text_ = std::move(provider);
        return *this;
    }
    LogView& LineLevel(std::function<LogLevel(size_t)> provider) {
        line_level_ = std::move(provider);
        return *this;
    }
    LogView& Follow(bool on = true) {
        follow_ = on;
        if (on) following_ = true;
        return *this;
    }
    bool Follow() const noexcept { return follow_; }
    bool Following() const noexcept { return following_; }

    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    LogView& SelectedIndex(ptrdiff_t index);
    bool CopySelection() const;

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    bool CapturesOverlay(Point p) const override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    float RowHeight() const noexcept { return 20.0f; }
    float ContentHeight() const noexcept;
    float MaxScroll() const;
    void ClampScroll();
    void ScrollToEnd();
    void PauseFollowIfScrolled();
    ptrdiff_t RowAt(Point local) const;
    Rect VerticalTrack() const noexcept;
    ScrollThumb Thumb(float expand) const noexcept;
    bool BeginScrollDrag(Point local);

    size_t item_count_ = 0;
    bool follow_ = true;
    bool following_ = true;
    ptrdiff_t selected_ = -1;
    ptrdiff_t hover_row_ = -1;
    float scroll_offset_ = 0.0f;
    float target_offset_ = 0.0f;
    float expand_progress_ = 0.0f;
    bool dragging_ = false;
    float drag_grab_ = 0.0f;
    std::function<void(size_t, std::wstring&)> line_text_;
    std::function<LogLevel(size_t)> line_level_;
    std::wstring draw_text_;
};

} // namespace lumen
