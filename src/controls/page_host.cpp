#include "lumen/PageHost.h"
#include "lumen/Painter.h"
#include "../core/window_impl.h"
#include <algorithm>

namespace lumen {
namespace {
constexpr float kSlide = 8.0f;
constexpr size_t kNone = static_cast<size_t>(-1);
}

PageHost::PageHost() {
    enter_.Snap(1.0f);
    exit_.Snap(1.0f);
}

size_t PageHost::Find(std::wstring_view id) const {
    for (size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i] == id) return i;
    }
    return kNone;
}

const std::wstring& PageHost::Current() const noexcept {
    static const std::wstring kEmpty;
    if (current_ >= ids_.size()) return kEmpty;
    return ids_[current_];
}

StackPanel& PageHost::Page(std::wstring_view id) {
    const size_t found = Find(id);
    if (found != kNone) return static_cast<StackPanel&>(Child(found));
    ids_.emplace_back(id);
    auto& page = Add<Column>();
    page.FillCross();
    if (current_ == kNone) {
        current_ = ids_.size() - 1;
        page.Visible(true);
    } else {
        page.Visible(false);
    }
    return page;
}

float PageHost::DurationOrSnap(float seconds) const {
    if (!window_ || MotionScale() <= 0.001f) return 0.0f;
    return seconds * MotionScale();
}

PageHost& PageHost::Show(std::wstring_view id) {
    const size_t next = Find(id);
    if (next == kNone || next == current_) return *this;
    FinishOutgoing();
    outgoing_ = current_;
    current_ = next;
    direction_ = (current_ > outgoing_) ? 1 : -1;
    if (outgoing_ < children_.size()) {
        Child(outgoing_).Visible(true);
        Child(outgoing_).Enabled(false);
    }
    Child(current_).Visible(true);
    Child(current_).Enabled(true);

    float enter_dur = 0.20f;
    float exit_dur = 0.12f;
    Ease enter_ease = Ease::CssEaseOut;
    Ease exit_ease = Ease::CssEaseIn;
    if (window_) {
        const Theme& theme = WindowImpl::ThemeOf(window_);
        enter_dur = theme.duration_normal;
        exit_dur = theme.duration_fast;
        enter_ease = theme.ease_enter;
        exit_ease = theme.ease_exit;
    }
    enter_dur = DurationOrSnap(enter_dur);
    exit_dur = DurationOrSnap(exit_dur);
    if (enter_dur <= 0.0f && exit_dur <= 0.0f) {
        enter_.Snap(1.0f);
        exit_.Snap(1.0f);
        FinishOutgoing();
    } else {
        enter_.Play(0.0f, 1.0f, enter_dur, enter_ease);
        exit_.Play(0.0f, 1.0f, exit_dur, exit_ease);
        Animate();
    }
    RelayoutParent();
    Invalidate();
    return *this;
}

void PageHost::FinishOutgoing() {
    if (outgoing_ >= children_.size() || outgoing_ == current_) {
        outgoing_ = kNone;
        return;
    }
    Child(outgoing_).Visible(false);
    Child(outgoing_).Enabled(true);
    outgoing_ = kNone;
}

Size PageHost::Measure(Size available, const Theme& theme) {
    if (outgoing_ < children_.size() && outgoing_ != current_) {
        MeasureChildAt(outgoing_, available, theme);
    }
    if (current_ >= children_.size()) return {};
    return MeasureChildAt(current_, available, theme);
}

void PageHost::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const auto place = [&](size_t index) {
        if (index >= children_.size()) return;
        const float h = std::max(ChildDesired(index).h, absolute.h);
        SetChildBounds(Child(index), {0.0f, 0.0f, absolute.w, h});
        ArrangeChildAt(index);
    };
    place(current_);
    if (outgoing_ != current_) place(outgoing_);
}

void PageHost::Draw(Painter&, const Theme&) {}

bool PageHost::OnAnimate(float dt) {
    bool more = Control::OnAnimate(dt);
    if (enter_.running) {
        more = enter_.Tick(dt) || more;
        Invalidate();
    }
    if (outgoing_ != kNone) {
        more = exit_.Tick(dt) || more;
        if (!exit_.running) FinishOutgoing();
        Invalidate();
    }
    return more;
}

void PageHost::ApplyChildTransform(size_t index, Painter& painter) const {
    float alpha = 1.0f;
    float dy = 0.0f;
    if (index == current_) {
        const float t = enter_.Value();
        alpha = t;
        dy = -kSlide * static_cast<float>(direction_) * (1.0f - t);
    } else if (index == outgoing_) {
        const float t = exit_.Value();
        alpha = 1.0f - t;
        dy = kSlide * static_cast<float>(direction_) * t;
    }
    painter.PushOpacity(alpha);
    painter.PushTranslate(0.0f, dy);
}

void PageHost::PushChildDrawAt(size_t index, Painter& painter) const {
    if (index != current_ && index != outgoing_) return;
    ApplyChildTransform(index, painter);
}

void PageHost::PopChildDrawAt(size_t index, Painter& painter) const {
    if (index != current_ && index != outgoing_) return;
    painter.PopTransform();
    painter.PopOpacity();
}

} // namespace lumen
