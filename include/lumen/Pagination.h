// lumen/Pagination.h — 分页器：‹ 1 … 4 5 6 … 12 ›，页码窗口 + 键盘左右。
// Events: OnNavigate / BindNavigate
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <vector>

namespace lumen {

class Pagination : public ControlOf<Pagination> {
public:
    Pagination() = default;

    Pagination& PageCount(size_t count);
    size_t PageCount() const noexcept { return count_; }
    // 编程翻页（1-based）；不触发 OnNavigate。
    Pagination& Current(size_t page);
    size_t Current() const noexcept { return current_; }
    Pagination& OnNavigate(std::function<void(size_t page)> handler) {
        navigate_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindNavigate(std::function<void(size_t page)> handler) {
        return navigate_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return count_ > 1; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    CursorShape CursorAt(Point local) const override;

private:
    struct Hit {
        Rect rect;
        int page = 0;            // 1-based；-1 = 上一页箭头，-2 = 下一页箭头，-3 = 省略号
        std::wstring label;      // 页码文本（输入路径预构建，绘制零分配）
    };
    void RebuildButtons(float width);
    void Navigate(size_t page);

    size_t count_ = 1;
    size_t current_ = 1;
    std::vector<Hit> buttons_;    // 布局缓存：Measure/Arrange/翻页时在输入路径重建
    int hover_ = -1;
    Signal<size_t> navigate_;
};

} // namespace lumen
