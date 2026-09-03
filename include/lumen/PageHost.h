// lumen/PageHost.h — 缓存多页并做切换编排：旧页淡出下沉、新页淡入上浮。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Animate.h"
#include "Panel.h"
#include <string>
#include <string_view>
#include <vector>

namespace lumen {

class PageHost : public PanelOf<PageHost> {
public:
    PageHost();

    // 按 id 取/建一页（Column）。已访问页缓存，不再重建。
    StackPanel& Page(std::wstring_view id);
    PageHost& Show(std::wstring_view id);
    const std::wstring& Current() const noexcept;
    // +1：新页从下方进入（选了更靠后的项）；-1：从上进入。
    int Direction() const noexcept { return direction_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool ClipChildren() const noexcept override { return true; }
    bool OnAnimate(float dt_seconds) override;
    void PushChildDrawAt(size_t index, Painter& painter) const override;
    void PopChildDrawAt(size_t index, Painter& painter) const override;

    size_t Find(std::wstring_view id) const;
    void FinishOutgoing();
    void ApplyChildTransform(size_t index, Painter& painter) const;
    float DurationOrSnap(float seconds) const;

    std::vector<std::wstring> ids_;
    size_t current_ = static_cast<size_t>(-1);
    size_t outgoing_ = static_cast<size_t>(-1);
    int direction_ = 1;
    Tween enter_{};
    Tween exit_{};
};

} // namespace lumen
