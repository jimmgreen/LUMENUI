#include "lumen/SplitView.h"
#include "lumen/Painter.h"
#include "lumen/Splitter.h"
#include <algorithm>

namespace lumen {

SplitView::SplitView() {
    pane_ = &Add<StackPanel>();
    content_ = &Add<StackPanel>();
    pane_->Clip(true);
    content_->Clip(true);
    splitter_ = &Add<Splitter>(Splitter::Orientation::Vertical);
    splitter_->OnDrag([this](float delta) {
        if (absolute_.IsEmpty()) return;
        const float min_w = compact_length_;
        const float max_w = std::max(min_w, absolute_.w - 80.0f);
        pane_w_ = Clamp(pane_w_ + delta, min_w, max_w);
        pane_length_ = std::max(96.0f, pane_w_);
        if (collapsed_ && pane_w_ > compact_length_ + 1.0f) collapsed_ = false;
        Place();
        Invalidate();
    });
}

StackPanel& SplitView::Pane() { return *pane_; }
StackPanel& SplitView::Content() { return *content_; }
Splitter& SplitView::Seam() { return *splitter_; }

SplitView& SplitView::Collapse(bool on) {
    if (collapsed_ == on) return *this;
    collapsed_ = on;
    Animate();
    toggled_.Emit(on);
    return *this;
}

float SplitView::PaneWidthTarget() const noexcept {
    if (!collapsed_) return pane_length_;
    return mode_ == PaneMode::Compact ? compact_length_ : 0.0f;
}

void SplitView::Place() {
    if (absolute_.IsEmpty() || !pane_ || !content_ || !splitter_) return;
    const float w = Clamp(pane_w_, 0.0f, absolute_.w);
    const float h = absolute_.h;
    const float hit = splitter_->Thickness();
    SetChildBounds(*pane_, {0.0f, 0.0f, w, h});
    SetChildBounds(*content_, {w, 0.0f, std::max(0.0f, absolute_.w - w), h});
    SetChildBounds(*splitter_, {w - hit * 0.5f, 0.0f, hit, h});
    for (size_t i = 0; i < ChildCount(); ++i) ArrangeChildAt(i);
}

Size SplitView::Measure(Size available, const Theme& theme) {
    // 两栏铺满（尺寸来自 Arrange），但必须向下测量：StackPanel::Arrange 依据
    // 子级 desired_ 缓存定位，跳过测量会让两栏内容全部按 0 尺寸摆放。
    for (size_t i = 0; i < ChildCount(); ++i) {
        MeasureChildAt(i, {available.w, available.h}, theme);
    }
    return {pane_length_ + 320.0f, 240.0f};
}

void SplitView::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    Place();
}

bool SplitView::OnAnimate(float dt_seconds) {
    if (splitter_ && splitter_->Dragging()) return Control::OnAnimate(dt_seconds);
    const float target = PaneWidthTarget();
    if (EaseTo(pane_w_, target, dt_seconds, 16.0f, 0.25f)) {
        Place();
        Invalidate();
        return true;
    }
    return Control::OnAnimate(dt_seconds);
}

void SplitView::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    const float w = Clamp(pane_w_, 0.0f, absolute_.w);
    painter.FillRect({absolute_.x, absolute_.y, w, absolute_.h}, theme.fill_input);
}

} // namespace lumen
