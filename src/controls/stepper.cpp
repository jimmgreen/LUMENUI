#include "lumen/Stepper.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <windows.h>
#include <algorithm>

namespace lumen {
namespace {
constexpr float kCircle = 20.0f;   // 步骤圆直径
constexpr float kGapX = 8.0f;      // 圆与标题间距
constexpr float kLineGap = 8.0f;   // 连接线与两侧内容（文字/圆）的间距
constexpr float kLineLen = 24.0f;  // 连接线净长
} // namespace

Stepper& Stepper::AddStep(std::wstring_view title) {
    titles_.emplace_back(title);
    RelayoutParent();
    return *this;
}

Stepper& Stepper::Current(size_t index) {
    const size_t clamped = std::min(index, titles_.empty() ? size_t{0} : titles_.size() - 1);
    if (current_ == clamped) return *this;
    const float from = static_cast<float>(current_);
    current_ = clamped;
    // 步进序号空间补间：跨多步跳转时进度头依次扫过中间线段。
    if (window_ && step_x_.size() == titles_.size()) {
        flow_.Play(from, static_cast<float>(clamped), 0.45f, Ease::Material);
        pop_.delay = 0.18f;   // 圆点在线段亮头到达后弹出
        pop_.Play(0.0f, 1.0f, 0.35f, Ease::OutBack);
        pop_ready_ = true;
        Animate();
    } else {
        SnapTo(clamped);
    }
    Invalidate();
    return *this;
}

void Stepper::SnapTo(size_t index) {
    flow_.Snap(static_cast<float>(index));
    pop_.Snap(1.0f);
    pop_ready_ = true;
}

void Stepper::Navigate(size_t step) {
    if (step >= current_) return;   // 只允许回跳到已完成步骤
    Current(step);                  // 复用补间路径
    step_changed_.Emit(step);
}

Size Stepper::Measure(Size, const Theme&) {
    step_x_.clear();
    step_w_.clear();
    float x = kGapX;
    for (const std::wstring& title : titles_) {
        const Size text = MeasureText(title, TextRole::Caption);
        text_h_ = text.h;
        const float w = kCircle + kGapX + text.w;
        step_x_.push_back(x);
        step_w_.push_back(w);
        // 步距 = 内容宽 + 上下线留白 + 净线长；线永远走在文字末尾与下一个圆之间。
        x += w + kLineGap * 2.0f + kLineLen;
    }
    return {std::max(x, 80.0f), kCircle + 6.0f};
}

bool Stepper::OnAnimate(float dt) {
    bool more = false;
    if (flow_.running) {
        flow_.Tick(dt);
        more = true;
    }
    if (pop_.running) {
        pop_.Tick(dt);
        more = true;
    }
    return more;
}

int Stepper::StepAt(float x) const {
    for (size_t i = 0; i < titles_.size(); ++i) {
        if (x >= step_x_[i] && x < step_x_[i] + step_w_[i]) return static_cast<int>(i);
    }
    return -1;
}

bool Stepper::OnKey(uint32_t vk) {
    if (titles_.empty()) return false;
    if (vk == VK_LEFT && current_ > 0) {
        Navigate(current_ - 1);
        return true;
    }
    return false;
}

void Stepper::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    const int step = StepAt(local.x);
    if (step >= 0) Navigate(static_cast<size_t>(step));
}

void Stepper::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    const int step = StepAt(local.x);
    const ptrdiff_t hover = step >= 0 && static_cast<size_t>(step) < current_ ? step : -1;
    if (hover != hover_) {
        hover_ = hover;
        Invalidate();
    }
}

void Stepper::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_ != -1) {
        hover_ = -1;
        Invalidate();
    }
}

void Stepper::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

CursorShape Stepper::CursorAt(Point local) const {
    const int step = StepAt(local.x);
    if (step >= 0 && static_cast<size_t>(step) < current_) return CursorShape::Hand;
    return CursorShape::Arrow;
}

void Stepper::Draw(Painter& painter, const Theme& theme) {
    if (titles_.empty() || absolute_.IsEmpty()) return;
    if (!pop_ready_) {
        pop_.Snap(1.0f);   // 首帧且从未走过 setter：当前圆点直接到位
        pop_ready_ = true;
    }
    const float cy = absolute_.y + kCircle * 0.5f + 3.0f;
    const float head = Clamp(flow_.Value(), 0.0f, static_cast<float>(titles_.size() - 1));
    const float pop = Clamp(pop_.Value(), 0.0f, 1.5f);
    for (size_t i = 0; i < titles_.size(); ++i) {
        const bool done = i < current_;
        const bool current = i == current_;
        const bool hot = static_cast<ptrdiff_t>(i) == hover_ && enabled_;
        const float cx = absolute_.x + step_x_[i] + kCircle * 0.5f;

        if (i + 1 < titles_.size()) {
            // 轨道恒暗；亮色进度头随 flow 扫过（回跳时缩回）。
            // 线段夹在上一步文字末尾与下一个圆之间，不与任何内容重叠。
            const float x0 = absolute_.x + step_x_[i] + step_w_[i] + kLineGap;
            const float x1 = absolute_.x + step_x_[i + 1] - kLineGap;
            painter.FillRect({x0, cy - 0.5f, std::max(0.0f, x1 - x0), 1.0f},
                             theme.stroke_divider);
            const float seg = Clamp(head - static_cast<float>(i), 0.0f, 1.0f);
            if (seg > 0.0f) {
                painter.FillRect({x0, cy - 0.5f, (x1 - x0) * seg, 1.0f}, theme.text_disabled);
            }
        }

        const Rect circle{cx - kCircle * 0.5f, cy - kCircle * 0.5f, kCircle, kCircle};
        if (done) {
            painter.FillRoundedRect(circle, kCircle * 0.5f,
                                    hot ? theme.text : theme.text_secondary);
            painter.DrawIcon(icon::kCheckMark, circle, 10.0f, theme.bg);
        } else if (current) {
            Color ring = theme.text;
            ring.a *= std::min(pop, 1.0f);
            painter.StrokeRoundedRect(circle, kCircle * 0.5f, ring);
            const float dot_r = 3.0f * pop;   // OutBack 轻过冲：先胀后定
            painter.FillRoundedRect({cx - dot_r, cy - dot_r, dot_r * 2.0f, dot_r * 2.0f},
                                    dot_r, theme.text);
        } else {
            painter.StrokeRoundedRect(circle, kCircle * 0.5f, theme.stroke_divider);
        }

        const Color color = done || current ? theme.text : theme.text_disabled;
        // DrawText 顶对齐：行盒在圆心带内垂直居中，光学中线与圆心/连接线对齐。
        painter.DrawText(titles_[i], {absolute_.x + step_x_[i] + kCircle + kGapX,
                                      cy - text_h_ * 0.5f,
                                      step_w_[i] - kCircle - kGapX + kGapX, text_h_},
                         TextRole::Caption, color);
    }
}

} // namespace lumen
