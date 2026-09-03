#include "lumen/RichLabel.h"
#include "lumen/Painter.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace lumen {

RichLabel& RichLabel::Add(std::wstring_view text) {
    runs_.push_back(Run{std::wstring(text), RunKind::Body, {}});
    wrap_width_ = -1.0f;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Strong(std::wstring_view text) {
    runs_.push_back(Run{std::wstring(text), RunKind::Strong, {}});
    wrap_width_ = -1.0f;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Secondary(std::wstring_view text) {
    runs_.push_back(Run{std::wstring(text), RunKind::Dim, {}});
    wrap_width_ = -1.0f;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Link(std::wstring_view text, std::function<void()> on_click) {
    runs_.push_back(Run{std::wstring(text), RunKind::Link, std::move(on_click)});
    wrap_width_ = -1.0f;
    RelayoutParent();
    return *this;
}

RichLabel& RichLabel::Clear() {
    runs_.clear();
    segs_.clear();
    wrap_width_ = -1.0f;
    RelayoutParent();
    return *this;
}

void RichLabel::Rebuild(float width) {
    segs_.clear();
    wrap_width_ = width;
    content_h_ = 20.0f;
    if (runs_.empty() || width < 8.0f) return;
    float x = 0.0f;
    float y = 0.0f;
    float line_h = 20.0f;
    for (size_t ri = 0; ri < runs_.size(); ++ri) {
        const Run& run = runs_[ri];
        const TextRole role = run.kind == RunKind::Strong ? TextRole::BodyStrong : TextRole::Body;
        size_t i = 0;
        while (i < run.text.size()) {
            if (run.text[i] == L'\n') {
                x = 0.0f;
                y += line_h;
                ++i;
                continue;
            }
            size_t j = i;
            while (j < run.text.size() && run.text[j] != L'\n' && !iswspace(run.text[j])) ++j;
            if (j == i) {
                while (j < run.text.size() && run.text[j] != L'\n' && iswspace(run.text[j])) ++j;
            }
            const std::wstring_view word(run.text.data() + i, j - i);
            const Size sz = MeasureText(word, role, 0.0f);
            line_h = std::max(line_h, sz.h);
            if (x > 0.5f && x + sz.w > width) {
                x = 0.0f;
                y += line_h;
            }
            segs_.push_back(Seg{ri, i, j - i, x, y, sz.w, sz.h});
            x += sz.w;
            i = j;
        }
    }
    content_h_ = y + line_h;
}

Size RichLabel::Measure(Size available, const Theme&) {
    const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 400.0f;
    if (std::fabs(wrap_width_ - w) > 0.5f) Rebuild(w);
    return {w, content_h_};
}

int RichLabel::HitRun(Point local) const {
    for (const Seg& seg : segs_) {
        if (local.x >= seg.x && local.x < seg.x + seg.w && local.y >= seg.y &&
            local.y < seg.y + std::max(seg.h, 16.0f)) {
            return static_cast<int>(seg.run);
        }
    }
    return -1;
}

void RichLabel::Draw(Painter& painter, const Theme& theme) {
    if (std::fabs(wrap_width_ - absolute_.w) > 0.5f) Rebuild(absolute_.w);
    for (const Seg& seg : segs_) {
        if (seg.length == 0 || seg.run >= runs_.size()) continue;
        const Run& run = runs_[seg.run];
        const TextRole role = run.kind == RunKind::Strong ? TextRole::BodyStrong : TextRole::Body;
        Color color = theme.text;
        if (run.kind == RunKind::Dim) color = theme.text_secondary;
        if (run.kind == RunKind::Link) {
            color = hover_run_ == static_cast<int>(seg.run) ? theme.text : theme.text_secondary;
        }
        const Rect slot{absolute_.x + seg.x, absolute_.y + seg.y, seg.w, std::max(seg.h, 16.0f)};
        painter.DrawText(std::wstring_view(run.text).substr(seg.begin, seg.length), slot, role,
                         color);
        if (run.kind == RunKind::Link) {
            painter.DrawLine({slot.x, slot.Bottom() - 2.0f}, {slot.Right(), slot.Bottom() - 2.0f},
                             color, 1.0f);
        }
    }
}

void RichLabel::OnMouseMove(Point local, uint32_t) {
    const int hit = HitRun(local);
    const int next = (hit >= 0 && hit < static_cast<int>(runs_.size()) &&
                      runs_[static_cast<size_t>(hit)].kind == RunKind::Link)
                         ? hit
                         : -1;
    if (next != hover_run_) {
        hover_run_ = next;
        Invalidate();
    }
}

void RichLabel::OnMouseUp(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    const int hit = HitRun(local);
    if (hit >= 0 && hit < static_cast<int>(runs_.size())) {
        const Run& run = runs_[static_cast<size_t>(hit)];
        if (run.kind == RunKind::Link && run.click) run.click();
    }
}

void RichLabel::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_run_ != -1) {
        hover_run_ = -1;
        Invalidate();
    }
}

CursorShape RichLabel::CursorAt(Point local) const {
    const int hit = HitRun(local);
    if (hit >= 0 && hit < static_cast<int>(runs_.size()) &&
        runs_[static_cast<size_t>(hit)].kind == RunKind::Link) {
        return CursorShape::Hand;
    }
    return CursorShape::Arrow;
}

} // namespace lumen
