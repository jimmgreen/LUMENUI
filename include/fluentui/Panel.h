// fluentui/Panel.h — 容器：普通 Panel（手动定位）与 StackPanel（自动堆叠布局）。
#pragma once
#include "Control.h"
#include <memory>
#include <type_traits>
#include <vector>

namespace fui {

class Panel : public Control {
public:
    ~Panel() override;

    // 子控件由父容器持有；返回引用供设置属性与事件。
    template <typename T, typename... Args>
    T& Add(Args&&... args) {
        static_assert(std::is_base_of_v<Control, T>, "Control 派生类型才能加入容器");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *owned;
        children_.push_back(std::move(owned));
        ref.parent_ = this;
        ref.window_ = window_;
        Relayout();
        return ref;
    }

    void Remove(Control& child);
    void Clear();
    size_t ChildCount() const noexcept { return children_.size(); }
    Control& Child(size_t index) const { return *children_[index]; }

    // 布局容器内部使用：直接设置子级矩形，不触发重排。
    void SetChildBounds(Control& child, const Rect& r);

    // 面板背景（默认透明）。
    void Background(Color color);
    Color Background() const noexcept { return background_; }

protected:
    friend class WindowImpl;

    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;

    void Relayout();  // 结构或内容变化后请求重新布局

    // 布局容器使用的子级访问辅助（Panel 是 Control 的友元，代子类操作 protected 成员）。
    bool ChildVisible(size_t index) const;
    const Size& ChildDesired(size_t index) const;
    void SetChildVisibility(size_t index, bool visible);
    Size MeasureChildAt(size_t index, Size available, const Theme& theme);
    void ArrangeChildAt(size_t index);   // 按子级当前相对 bounds_ 排布到父级绝对坐标

    std::vector<std::unique_ptr<Control>> children_;
    Color background_{0.0f, 0.0f, 0.0f, 0.0f};
};

class StackPanel : public Panel {
public:
    enum class Orientation { Vertical, Horizontal };

    StackPanel() = default;
    explicit StackPanel(Orientation orientation) : orientation_(orientation) {}

    StackPanel& SetOrientation(Orientation value) { orientation_ = value; Relayout(); return *this; }
    StackPanel& Spacing(float value) { spacing_ = value; Relayout(); return *this; }
    StackPanel& Padding(float uniform) { return Padding(uniform, uniform); }
    StackPanel& Padding(float horizontal, float vertical) {
        padding_h_ = horizontal;
        padding_v_ = vertical;
        Relayout();
        return *this;
    }

protected:
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;

    Orientation orientation_ = Orientation::Vertical;
    float spacing_ = 0.0f;
    float padding_h_ = 0.0f;
    float padding_v_ = 0.0f;
};

} // namespace fui
