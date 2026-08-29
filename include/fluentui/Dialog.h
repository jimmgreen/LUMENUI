// fluentui/Dialog.h — 窗口内模态对话框（居中卡片 + 半透明遮罩）。
#pragma once
#include "Panel.h"
#include <functional>
#include <string>

namespace fui {

class Dialog : public Panel {
public:
    const std::wstring& Title() const noexcept { return title_; }
    Dialog& Title(std::wstring_view value) { title_ = value; Invalidate(); return *this; }
    const std::wstring& Message() const noexcept { return message_; }
    Dialog& Message(std::wstring_view value) { message_ = value; RelayoutParent(); return *this; }

    Dialog& PrimaryButton(std::wstring_view label, std::function<void()> action);
    Dialog& SecondaryButton(std::wstring_view label, std::function<void()> action);
    Dialog& DefaultClose();   // 无按钮；点击遮罩或 Esc 关闭

    // 内容需要更复杂布局时，直接向 Dialog 添加控件并手动 SetBounds（卡片坐标，DIP）。

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnKey(uint32_t vk) override;

    void Close();   // 关闭所在窗口的对话框
    void UpdateHot(Point local);

    std::wstring title_;
    std::wstring message_;
    std::wstring primary_label_;
    std::wstring secondary_label_;
    std::function<void()> primary_action_;
    std::function<void()> secondary_action_;
    bool default_close_ = false;
    bool primary_hot_ = false;
    bool secondary_hot_ = false;
    bool primary_press_ = false;
    bool secondary_press_ = false;
    // 按钮矩形在 Draw 前由 Arrange 计算（卡片局部坐标）
    Rect primary_rect_;
    Rect secondary_rect_;
};

} // namespace fui
