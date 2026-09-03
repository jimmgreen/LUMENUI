// lumen/ToolTip.h — 自定义悬停提示内容（窗口层 overlay 绘制，不进宿主布局）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Panel.h"
#include <algorithm>

namespace lumen {

class ToolTip : public StackPanel {
public:
    ToolTip();

    // 内容最大宽度（DIP）。默认 268，与字符串提示同一上限。
    ToolTip& MaxWidth(float value) {
        max_width_ = std::max(80.0f, value);
        Relayout();
        return *this;
    }
    float MaxWidth() const noexcept { return max_width_; }
    // 右上角关闭钮。默认开启（与带标题的字符串提示一致）。
    ToolTip& Closable(bool value) {
        closable_ = value;
        return *this;
    }
    bool Closable() const noexcept { return closable_; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;

    float max_width_ = 268.0f;
    bool closable_ = true;
};

} // namespace lumen
