// lumen/Chart.h — 仪表盘图表：一份数据入口，多种圆角单色形态。绘制路径零堆。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace lumen {

enum class ChartKind { Line, Bar, Area, Donut, Heatmap, Radar, Funnel, Bullet };
enum class ChartBar { Vertical, Horizontal };

struct ChartSlice {
    std::wstring_view label;
    float value = 0.0f;
};

class Chart : public ControlOf<Chart> {
public:
    static constexpr size_t kMaxSamples = 256;
    static constexpr size_t kMaxSlices = 16;
    static constexpr size_t kMaxLabels = 16;
    static constexpr size_t kMaxHeatCols = 24;
    static constexpr size_t kMaxHeatRows = 8;

    Chart& Kind(ChartKind value) {
        kind_ = value;
        RelayoutParent();
        return *this;
    }
    ChartKind Kind() const noexcept { return kind_; }

    Chart& Bar(ChartBar value) {
        bar_ = value;
        Invalidate();
        return *this;
    }
    ChartBar Bar() const noexcept { return bar_; }

    Chart& Header(std::wstring_view title, std::wstring_view value = {}) {
        title_ = title;
        value_text_ = value;
        RelayoutParent();
        return *this;
    }
    Chart& Hint(std::wstring_view text) {
        hint_ = text;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Title() const noexcept { return title_; }
    const std::wstring& ValueText() const noexcept { return value_text_; }
    const std::wstring& Hint() const noexcept { return hint_; }

    Chart& PreferredSize(Size size) {
        preferred_ = size;
        RelayoutParent();
        return *this;
    }
    Size PreferredSize() const noexcept { return preferred_; }

    Chart& Count(size_t count);
    size_t Count() const noexcept { return count_; }
    Chart& Values(std::function<float(size_t)> provider);
    Chart& Values(std::span<const float> values);
    Chart& Values(std::initializer_list<float> values) {
        return Values(std::span<const float>(values.begin(), values.size()));
    }

    Chart& Targets(std::span<const float> values);
    Chart& Targets(std::initializer_list<float> values) {
        return Targets(std::span<const float>(values.begin(), values.size()));
    }

    Chart& Baseline(std::span<const float> values);
    Chart& Baseline(std::initializer_list<float> values) {
        return Baseline(std::span<const float>(values.begin(), values.size()));
    }
    Chart& SeriesName(std::wstring_view name) {
        series_name_ = name;
        Invalidate();
        return *this;
    }
    Chart& BaselineName(std::wstring_view name) {
        baseline_name_ = name;
        Invalidate();
        return *this;
    }

    Chart& XLabels(std::initializer_list<std::wstring_view> labels);
    Chart& Slices(std::initializer_list<ChartSlice> slices);
    Chart& Grid(size_t cols, size_t rows);
    Chart& Cell(std::function<float(size_t x, size_t y)> provider);

    // 图例：Line/Area 的 0=主系列、1=基线；Donut 为切片下标。点击图例切换。
    bool SeriesVisible(size_t index) const noexcept;
    Chart& SeriesVisible(size_t index, bool on);
    size_t LegendCount() const noexcept;
    Rect LegendBounds(size_t index) const noexcept;  // 局部 DIP

    // 当前绘制用的值（补间中为插值；函数数据源无补间）。
    float DisplayValue(size_t index) const noexcept;
    // 视口窗口，0..1 映射到样本下标。滚轮缩放、拖拽平移、双击复位。
    float ViewStart() const noexcept { return view0_; }
    float ViewEnd() const noexcept { return view1_; }
    Chart& ResetView();

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnMouseDoubleClick(Point local) override;
    bool OnWheel(float delta) override;
    bool OnAnimate(float dt_seconds) override;
    CursorShape CursorAt(Point local) const override;
    bool HitTransparent() const noexcept override { return false; }
    bool PrefersDragOverPan() const noexcept override { return panning_; }

    Rect PlotBounds() const noexcept;
    void SampleSeries(float* ys, size_t& n, float& mn, float& mx, float width_dip,
                      bool normalize = true) const;
    size_t HitIndex(Point local) const;
    const std::wstring* LabelAt(size_t index, size_t count) const;
    bool Cartesian() const noexcept;
    bool ShowsLegend() const noexcept;
    int LegendHit(Point local) const noexcept;
    float DisplayAt(size_t src_index, size_t src_count) const noexcept;
    void ClampView() noexcept;
    void SnapBarView(size_t src) noexcept;
    void BarWindow(size_t& first, size_t& n) const noexcept;
    void ZoomAt(float pivot01, float factor);
    void DrawLegend(Painter& painter, const Theme& theme) const;
    void DrawCartesianHover(Painter& painter, const Theme& theme, const Rect& box, const Point* src,
                            const float* ys, size_t n, bool has_b, const Point* bsrc,
                            const float* bys) const;

    ChartKind kind_ = ChartKind::Line;
    ChartBar bar_ = ChartBar::Vertical;
    Size preferred_{};
    std::wstring title_;
    std::wstring value_text_;
    std::wstring hint_;

    float values_[kMaxSamples]{};
    float shown_[kMaxSamples]{};
    float from_[kMaxSamples]{};
    size_t count_ = 0;
    size_t stored_ = 0;
    size_t from_n_ = 0;
    bool use_store_ = false;
    bool shown_ready_ = false;
    std::function<float(size_t)> values_fn_;
    Tween value_tween_{};

    float targets_[kMaxSamples]{};
    size_t target_count_ = 0;
    float baseline_[kMaxSamples]{};
    size_t baseline_count_ = 0;
    std::wstring series_name_;
    std::wstring baseline_name_;
    bool series_hidden_[kMaxSlices]{};

    size_t hover_i_ = static_cast<size_t>(-1);
    float bar_scale_[8]{};
    float view0_ = 0.0f;
    float view1_ = 1.0f;
    bool panning_ = false;
    Point pan_local_{};
    float pan_v0_ = 0.0f;
    float pan_v1_ = 1.0f;

    std::wstring labels_[kMaxLabels];
    size_t label_count_ = 0;

    struct Slice {
        std::wstring label;
        float value = 0.0f;
    };
    Slice slices_[kMaxSlices];
    size_t slice_count_ = 0;

    size_t grid_cols_ = 0;
    size_t grid_rows_ = 0;
    std::function<float(size_t, size_t)> cell_fn_;
};

} // namespace lumen
