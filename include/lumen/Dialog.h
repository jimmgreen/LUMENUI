// lumen/Dialog.h — 窗口内模态对话框（居中卡片 + 半透明遮罩）。
// Events: OnResult / BindResult
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Button.h"
#include "Panel.h"
#include "Animate.h"
#include "Signal.h"
#include <functional>
#include <optional>
#include <string>

namespace lumen {

enum class DialogSize : uint8_t { Compact, Standard, Wide };
enum class DialogResult : uint8_t { None, Primary, Secondary, Close };
enum class DialogCommand : uint8_t { Auto, None, Primary, Secondary, Close };

struct DialogButton {
    std::wstring label;
    std::function<void()> action;
};

struct DialogSpec {
    std::wstring title;
    std::wstring message;
    DialogButton primary;
    DialogButton secondary;
    DialogButton close;
    DialogSize size = DialogSize::Standard;
    DialogCommand default_button = DialogCommand::Auto;
    DialogCommand cancel_button = DialogCommand::Auto;
    std::function<void(DialogResult)> on_result;
    std::function<void(Panel&)> content;
};

class Dialog : public PanelOf<Dialog> {
public:
    Dialog();
    ~Dialog() override;
    const std::wstring& Title() const noexcept { return title_; }
    Dialog& Title(std::wstring_view value) { title_ = value; RelayoutParent(); return *this; }
    const std::wstring& Message() const noexcept { return message_; }
    Dialog& Message(std::wstring_view value) { message_ = value; RelayoutParent(); return *this; }

    Dialog& CardSize(DialogSize value);
    DialogSize CardSize() const noexcept { return size_; }
    // 自定义卡片宽（DIP）；0 则跟 CardSize 档。
    Dialog& CardWidth(float dip);
    float CardWidth() const noexcept { return card_width_; }

    Dialog& PrimaryButton(std::wstring_view label, std::function<void()> action = {});
    Dialog& SecondaryButton(std::wstring_view label, std::function<void()> action = {});
    Dialog& CloseButton(std::wstring_view label, std::function<void()> action = {});
    Dialog& DefaultClose();   // 无页脚钮；点击遮罩或 Esc 关闭

    Dialog& DefaultButton(DialogCommand command);
    Dialog& CancelButton(DialogCommand command);
    DialogCommand DefaultButton() const noexcept { return default_cmd_; }
    DialogCommand CancelButton() const noexcept { return cancel_cmd_; }

    Dialog& OnResult(std::function<void(DialogResult)> handler);
    Connection BindResult(std::function<void(DialogResult)> handler);
    DialogResult Result() const noexcept { return result_; }

    void Dismiss(DialogResult result = DialogResult::None);

    // 额外子级排在正文与页脚之间（交叉轴拉满卡片内宽），不必手动 SetBounds。

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool OnKey(uint32_t vk) override;
    bool OnAnimate(float dt_seconds) override;
    void PushChildDraw(Painter& painter) const override;
    void PopChildDraw(Painter& painter) const override;

    void Close();
    void ArmEnter();
    void PushEnter(Painter& painter) const;
    void PopEnter(Painter& painter) const;
    void CompleteResult();
    bool FooterVisible() const noexcept;
    bool IsFooterChild(const Control& child) const noexcept;
    DialogCommand ResolvedDefault() const noexcept;
    DialogCommand ResolvedCancel() const noexcept;
    void InvokeCommand(DialogCommand command);
    float ResolvedCardWidth(float available_w) const noexcept;
    Button* ButtonFor(DialogCommand command) const noexcept;

    std::wstring title_;
    std::wstring message_;
    std::function<void()> primary_action_;
    std::function<void()> secondary_action_;
    std::function<void()> close_action_;
    Signal<DialogResult> result_changed_;
    DialogSize size_ = DialogSize::Standard;
    DialogCommand default_cmd_ = DialogCommand::Auto;
    DialogCommand cancel_cmd_ = DialogCommand::Auto;
    DialogResult result_ = DialogResult::None;
    float card_width_ = 0.0f;
    bool default_close_ = false;
    bool closing_ = false;
    bool result_signaled_ = false;
    Button* primary_btn_ = nullptr;
    Button* secondary_btn_ = nullptr;
    Button* close_btn_ = nullptr;
    Tween enter_{};
};

} // namespace lumen
