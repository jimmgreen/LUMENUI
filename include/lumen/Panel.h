// lumen/Panel.h — 容器：Panel（手动定位）、Row/Column（堆叠）与 Grid（轨道）。
// Events: OnClick / BindClick
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace lumen {

class Panel : public ControlOf<Panel> {
public:
    Panel() = default;
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;
    Panel(Panel&& other) noexcept;
    Panel& operator=(Panel&& other) noexcept;
    ~Panel() override;
    Panel* AsPanel() noexcept override { return this; }
    const Panel* AsPanel() const noexcept override { return this; }

    // 子控件由父容器持有；返回引用供设置属性与事件。
    template <typename T, typename... Args>
    T& Add(Args&&... args) {
        static_assert(std::is_base_of_v<Control, T>, "Control 派生类型才能加入容器");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *owned;
        owned->CommitRef();
        children_.push_back(std::move(owned));
        ref.parent_ = this;
        ref.window_ = window_;
        ref.BindWindowDeep();   // 本面板已入树时，新子树整体补绑定
        Relayout();
        return ref;
    }
    Panel& Add(std::unique_ptr<Control> child);

    // 声明式嵌套：右值控件 move 进 unique_ptr，整包结束后 Relayout 一次。
    template <typename... Ts>
    Panel& Children(Ts&&... xs) {
        (AdoptOne(std::forward<Ts>(xs)), ...);
        Relayout();
        return *this;
    }

    void Remove(Control& child);
    void Clear();
    size_t ChildCount() const noexcept { return children_.size(); }
    Control* ChildAt(size_t index) const noexcept {
        return index < children_.size() ? children_[index].get() : nullptr;
    }
    Control& Child(size_t index) const {
        if (index >= children_.size()) {
            Control::DebugTrap(L"LUMEN_CHECK: Panel::Child index out of range");
            std::terminate();
        }
        return *children_[index];
    }

    // 布局容器内部使用：直接设置子级矩形，不触发重排。
    void SetChildBounds(Control& child, const Rect& r);

    // 面板背景（默认透明，直角填充）。
    Panel& Background(Color color);
    Color Background() const noexcept { return background_; }

    // Fluent 卡片容器：圆角填充 + 细描边（alpha 0 表示不画）。自定义颜色不随主题切换。
    Panel& Card(Color fill, Color stroke, float radius);

    // 按 token 风格设置卡片（颜色取自当前主题，随光效强度更新）。
    // Lumen = 聚光卡片：鼠标跟随光斑 + 边缘折射光环（自动开启聚光）。
    enum class CardStyle { Flyout, Input, Subtle, Lumen };
    Panel& Card(CardStyle style, float radius);

    // 空区点击（子控件优先命中）。用于整卡可点的展示面。
    Panel& OnClick(std::function<void()> handler) {
        click_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClick(std::function<void()> handler) { return click_.Connect(std::move(handler)); }
    // 骨架扫光：仅在悬停/聚光期间运行，禁止空转。
    Panel& Shimmer(bool value);

    // 子级绘制裁到本控件矩形（手风琴收起时隐藏溢出内容）。
    Panel& Clip(bool value);
    virtual bool ClipChildren() const noexcept { return clip_children_; }
    // ClipChildren 时的裁剪区（绝对坐标）；默认整控件。表体/滚动视口可收窄。
    virtual Rect ChildrenClipBounds() const noexcept { return AbsoluteBounds(); }
    // 子级绘完后的覆盖层（滚动条）。默认空。
    virtual void DrawOverlay(Painter& painter, const Theme& theme) {
        (void)painter;
        (void)theme;
    }
    // Viewbox 等把子级画在缩放坐标系里：默认恒等。HitTree / ToLocal 走同一套映射。
    virtual Point MapToChildren(Point window_dip) const { return window_dip; }
    virtual Rect MapClipToChildren(const Rect& clip) const { return clip; }
    virtual void PushChildDraw(Painter& painter) const { (void)painter; }
    virtual void PopChildDraw(Painter& painter) const { (void)painter; }
    // 逐子级绘制包装（PageHost 入场/退场）。默认空；必须成对。
    virtual void PushChildDrawAt(size_t index, Painter& painter) const {
        (void)index;
        (void)painter;
    }
    virtual void PopChildDrawAt(size_t index, Painter& painter) const {
        (void)index;
        (void)painter;
    }

protected:
    friend class WindowImpl;
    friend void DrawControlTree(Painter& painter, const Theme& theme, Control* root);
    friend void DrawControlTree(Painter& painter, const Theme& theme, Control* root,
                                const Rect& clip);

    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseEnter() override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnAnimate(float dt_seconds) override;

    void Relayout();  // 结构或内容变化后请求重新布局

    template <typename T>
    void AdoptOne(T&& x) {
        using U = std::decay_t<T>;
        static_assert(std::is_base_of_v<Control, U>, "Control 派生类型才能加入容器");
        if (x.parent_ != nullptr) {
            Control::DebugTrap(L"LUMEN_CHECK: Children() move of an already-parented control");
        }
        auto owned = std::make_unique<U>(std::move(x));
        x.AbandonRef();
        U& ref = *owned;
        owned->CommitRef();
        children_.push_back(std::move(owned));
        ref.parent_ = this;
        ref.window_ = window_;
        ref.BindWindowDeep();
    }

    // 布局容器使用的子级访问辅助（Panel 是 Control 的友元，代子类操作 protected 成员）。
    bool ChildVisible(size_t index) const;
    const Size& ChildDesired(size_t index) const;
    void SetChildVisibility(size_t index, bool visible);
    Size MeasureChildAt(size_t index, Size available, const Theme& theme);
    void ArrangeChildAt(size_t index);   // 按子级当前相对 bounds_ 排布到父级绝对坐标
    void SyncChildAbsolute(size_t index);  // 只写绝对矩形，不递归 Arrange 子树
    void PaintShimmer(Painter& painter);
    // 聚光卡片用：光斑不穿透交互子控件，控件矩形垫回碳底（追光避开控件本身）。
    // 垫会被沿途 ClipChildren 面板的视口裁剪（表体缓冲行不得在表体外露垫）。
    void AvoidControls(Painter& painter, const Theme& theme);
    void AvoidWalk(Painter& painter, const Theme& theme, const Rect& clip);

    std::vector<std::unique_ptr<Control>> children_;
    Color background_{0.0f, 0.0f, 0.0f, 0.0f};
    Color card_fill_{0.0f, 0.0f, 0.0f, 0.0f};
    Color card_stroke_{0.0f, 0.0f, 0.0f, 0.0f};
    float card_radius_ = 0.0f;
    CardStyle card_style_ = CardStyle::Flyout;
    bool use_card_style_ = false;
    bool shimmer_ = false;
    bool clip_children_ = false;
    float shimmer_phase_ = 0.0f;
    Signal<> click_;
};

template <class D>
class PanelOf : public Panel {
public:
    PanelOf() = default;
    PanelOf(const PanelOf&) = delete;
    PanelOf& operator=(const PanelOf&) = delete;
    PanelOf(PanelOf&&) noexcept = default;
    PanelOf& operator=(PanelOf&&) noexcept = default;

    bool Visible() const noexcept { return Control::Visible(); }
    bool Enabled() const noexcept { return Control::Enabled(); }
    const std::wstring& ToolTip() const noexcept { return Control::ToolTip(); }
    const std::wstring& AccessibleName() const noexcept { return Control::AccessibleName(); }
    bool Spotlight() const noexcept { return Control::Spotlight(); }

    D& Visible(bool v) {
        Control::Visible(v);
        return Self();
    }
    D& Enabled(bool v) {
        Control::Enabled(v);
        return Self();
    }
    D& ToolTip(std::wstring_view text) {
        Control::ToolTip(text);
        return Self();
    }
    D& ToolTip(std::unique_ptr<class ToolTip> content) {
        Control::ToolTip(std::move(content));
        return Self();
    }
    D& ToolTip(std::string_view utf8) { return ToolTip(U8(utf8)); }
    D& Grow(float weight = 1.0f) {
        Control::Grow(weight);
        return Self();
    }
    D& FillCross(bool value = true) {
        Control::FillCross(value);
        return Self();
    }
    D& Margin(float uniform) {
        Control::Margin(uniform);
        return Self();
    }
    D& Margin(float horizontal, float vertical) {
        Control::Margin(horizontal, vertical);
        return Self();
    }
    D& Margin(Thickness thickness) {
        Control::Margin(thickness);
        return Self();
    }
    D& MinSize(Size size) {
        Control::MinSize(size);
        return Self();
    }
    D& MaxSize(Size size) {
        Control::MaxSize(size);
        return Self();
    }
    D& Style(const ThemeOverride& o) {
        Control::Style(o);
        return Self();
    }
    D& Density(lumen::Density d) {
        Control::Density(d);
        return Self();
    }
    D& Spotlight(bool enabled) {
        Control::Spotlight(enabled);
        return Self();
    }
    D& ContextMenu(Menu menu) {
        Control::ContextMenu(std::move(menu));
        return Self();
    }
    D& AccessibleName(std::wstring_view name) {
        Control::AccessibleName(name);
        return Self();
    }
    D& AccessibleName(std::string_view utf8) { return AccessibleName(U8(utf8)); }
    D& SetBounds(const Rect& r) {
        Control::SetBounds(r);
        return Self();
    }
    D& Focus() {
        Control::Focus();
        return Self();
    }
    D& Background(Color color) {
        Panel::Background(color);
        return Self();
    }
    D& Card(Color fill, Color stroke, float radius) {
        Panel::Card(fill, stroke, radius);
        return Self();
    }
    D& Card(CardStyle style, float radius) {
        Panel::Card(style, radius);
        return Self();
    }
    D& OnClick(std::function<void()> handler) {
        Panel::OnClick(std::move(handler));
        return Self();
    }
    D& Shimmer(bool value) {
        Panel::Shimmer(value);
        return Self();
    }
    D& Clip(bool value) {
        Panel::Clip(value);
        return Self();
    }
    template <typename... Ts>
    D& Children(Ts&&... xs) {
        Panel::Children(std::forward<Ts>(xs)...);
        return Self();
    }
    using Panel::Add;
    D& Add(std::unique_ptr<Control> child) {
        Panel::Add(std::move(child));
        return Self();
    }

protected:
    D& Self() noexcept { return static_cast<D&>(*this); }
    const D& Self() const noexcept { return static_cast<const D&>(*this); }
};

class StackPanel : public PanelOf<StackPanel> {
public:
    enum class Orientation { Vertical, Horizontal };
    using Orient = Orientation;
    using CrossAlign = lumen::CrossAlign;
    using MainAlign = lumen::MainAlign;

    StackPanel() = default;
    explicit StackPanel(Orientation orientation) : orientation_(orientation) {}

    StackPanel& Orientation(Orient value) { orientation_ = value; Relayout(); return *this; }
    Orient Orientation() const noexcept { return orientation_; }
    StackPanel& Spacing(float value) { spacing_ = value; Relayout(); return *this; }
    StackPanel& Padding(float uniform) { return Padding(uniform, uniform); }
    StackPanel& Padding(float horizontal, float vertical) {
        padding_h_ = horizontal;
        padding_v_ = vertical;
        Relayout();
        return *this;
    }
    StackPanel& AlignCross(CrossAlign value) { cross_align_ = value; Relayout(); return *this; }
    StackPanel& AlignMain(MainAlign value) { main_align_ = value; Relayout(); return *this; }
    StackPanel& Comfortable();
    StackPanel& Dense();

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;

    Orient orientation_ = Orientation::Vertical;
    CrossAlign cross_align_ = CrossAlign::Stretch;
    MainAlign main_align_ = MainAlign::Start;
    float spacing_ = 0.0f;
    float padding_h_ = 0.0f;
    float padding_v_ = 0.0f;
};

class Row : public StackPanel {
public:
    Row() : StackPanel(Orientation::Horizontal) {}
};

class Column : public StackPanel {
public:
    Column() : StackPanel(Orientation::Vertical) {}
};

using HStack = Row;
using VStack = Column;

// 主轴排满后折行。Horizontal：从左到右、不够则换行；Vertical：从上到下、不够则换列。
class WrapPanel : public PanelOf<WrapPanel> {
public:
    enum class Orientation { Horizontal, Vertical };
    using Orient = Orientation;

    WrapPanel() = default;
    explicit WrapPanel(Orientation orientation) : orientation_(orientation) {}

    WrapPanel& Orientation(Orient value) { orientation_ = value; Relayout(); return *this; }
    WrapPanel& Gap(float value) { return Gap(value, value); }
    WrapPanel& Gap(float column, float row) {
        gap_x_ = std::max(0.0f, column);
        gap_y_ = std::max(0.0f, row);
        Relayout();
        return *this;
    }
    WrapPanel& Padding(float uniform) { return Padding(uniform, uniform); }
    WrapPanel& Padding(float horizontal, float vertical) {
        padding_h_ = horizontal;
        padding_v_ = vertical;
        Relayout();
        return *this;
    }

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;

    Orient orientation_ = Orientation::Horizontal;
    float gap_x_ = 0.0f;
    float gap_y_ = 0.0f;
    float padding_h_ = 0.0f;
    float padding_v_ = 0.0f;
};

// 主轴空隙。默认 Grow(1) 吃剩余空间；Spacer(12) 为固定缝，不参与 Grow。
class Spacer : public ControlOf<Spacer> {
public:
    Spacer();
    explicit Spacer(float gap);

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }

    float gap_ = 0.0f;
};

// 等分列：Grid(2)。加权轨道：Grid(1, 0, 1) 为 1fr / auto / 1fr（顶栏左中右）。
// 0 = 内容宽，>0 = fr 权重。子级按行主序填入，单元格拉伸。
class Grid : public PanelOf<Grid> {
public:
    explicit Grid(int equal_columns = 2);
    template <typename... Rest>
    Grid(double first, Rest... rest)
        : tracks_{static_cast<float>(first), static_cast<float>(rest)...} {}

    Grid& Columns(int equal_columns);
    Grid& Gap(float value) { return Gap(value, value); }
    Grid& Gap(float column, float row);
    Grid& Padding(float uniform) { return Padding(uniform, uniform); }
    Grid& Padding(float horizontal, float vertical);

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;

    std::vector<float> tracks_;
    float gap_x_ = 0.0f;
    float gap_y_ = 0.0f;
    float padding_h_ = 0.0f;
    float padding_v_ = 0.0f;
};

// 层叠：子级叠在同一矩形。默认居中；FillCross 拉满两轴。
class ZStack : public PanelOf<ZStack> {
public:
    using Align = lumen::CrossAlign;

    ZStack& Padding(float uniform) { return Padding(uniform, uniform); }
    ZStack& Padding(float horizontal, float vertical) {
        padding_h_ = horizontal;
        padding_v_ = vertical;
        Relayout();
        return *this;
    }
    ZStack& AlignH(Align value) { align_h_ = value; Relayout(); return *this; }
    ZStack& AlignV(Align value) { align_v_ = value; Relayout(); return *this; }

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;

    Align align_h_ = Align::Center;
    Align align_v_ = Align::Center;
    float padding_h_ = 0.0f;
    float padding_v_ = 0.0f;
};

} // namespace lumen
