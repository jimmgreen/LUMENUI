// lumen/RichLabel.h — 混排正文：普通 / 加粗 / 次要 / 内联链接，按容器宽换行。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

class RichLabel : public ControlOf<RichLabel> {
public:
    RichLabel& Add(std::wstring_view text);
    RichLabel& Strong(std::wstring_view text);
    RichLabel& Secondary(std::wstring_view text);
    RichLabel& Link(std::wstring_view text, std::function<void()> on_click);
    RichLabel& Clear();

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    CursorShape CursorAt(Point local) const override;

    enum class RunKind { Body, Strong, Dim, Link };
    struct Run {
        std::wstring text;
        RunKind kind = RunKind::Body;
        std::function<void()> click;
    };
    struct Seg {
        size_t run = 0;
        size_t begin = 0;
        size_t length = 0;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    void Rebuild(float width);
    int HitRun(Point local) const;

    std::vector<Run> runs_;
    std::vector<Seg> segs_;
    float wrap_width_ = -1.0f;
    float content_h_ = 20.0f;
    int hover_run_ = -1;
};

} // namespace lumen
