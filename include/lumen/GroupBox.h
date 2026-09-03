// lumen/GroupBox.h — 带标题的轻量分组（卡片光感 + 标题打断顶边）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include <string>

namespace lumen {

class GroupBox : public PanelOf<GroupBox> {
public:
    GroupBox();
    explicit GroupBox(std::wstring_view title);

    GroupBox& Title(std::wstring_view value) {
        title_ = value;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Title() const noexcept { return title_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;

    void RelayoutParent();

    std::wstring title_;
    float top_ = 0.0f;
};

} // namespace lumen
