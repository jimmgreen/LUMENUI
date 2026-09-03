// lumen/StatusBar.h — 窗口底栏：路径（左，省略）/ 缩放 / 计数（右）。
// Events: OnInvoked / BindInvoked
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>
#include <vector>

namespace lumen {

enum class StatusBarAlign { Leading, Trailing };

class StatusBar : public ControlOf<StatusBar> {
public:
    static constexpr float kHeight = 28.0f;

    StatusBar& AddItem(std::wstring_view id, std::wstring_view text,
                       StatusBarAlign align = StatusBarAlign::Leading);
    StatusBar& ItemText(std::wstring_view id, std::wstring_view text);
    StatusBar& ItemGlyph(std::wstring_view id, std::wstring_view glyph);
    bool RemoveItem(std::wstring_view id);
    size_t ItemCount() const noexcept { return items_.size(); }
    const std::wstring& ItemText(std::wstring_view id) const;

    StatusBar& Path(std::wstring_view text);
    const std::wstring& Path() const;
    StatusBar& Zoom(std::wstring_view text);
    const std::wstring& Zoom() const;
    StatusBar& CountText(std::wstring_view text);
    const std::wstring& CountText() const;

    StatusBar& OnInvoked(std::function<void(std::wstring_view id)> handler) {
        invoked_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindInvoked(std::function<void(std::wstring_view id)> handler) {
        return invoked_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::StatusBar;
    }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? Path() : accessible_name_;
    }
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    CursorShape CursorAt(Point local) const override;

    void RelayoutParent();

private:
    struct Item {
        std::wstring id;
        std::wstring text;
        std::wstring glyph;
        StatusBarAlign align = StatusBarAlign::Leading;
    };
    Item* Find(std::wstring_view id) noexcept;
    const Item* Find(std::wstring_view id) const noexcept;
    StatusBar& Upsert(std::wstring_view id, std::wstring_view text, StatusBarAlign align,
                      std::wstring_view glyph = {});
    void Rebuild(float width);
    int HitIndex(Point local) const noexcept;
    float PreferredWidth(const Item& item) const;

    std::vector<Item> items_;
    std::vector<Rect> slots_;
    std::vector<float> seps_;
    Signal<std::wstring_view> invoked_;
    int hover_ = -1;
};

} // namespace lumen
