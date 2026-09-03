#include "lumen/EmptyState.h"
#include "lumen/App.h"
#include "lumen/Button.h"
#include "lumen/Icons.h"
#include "lumen/Label.h"
#include "lumen/IconView.h"

namespace lumen {

EmptyState::EmptyState() {
    Orientation(Orientation::Vertical);
    Spacing(8.0f);
    Padding(24.0f, 28.0f);
    AlignCross(CrossAlign::Center);
    AlignMain(MainAlign::Center);

    icon_ = &Add<IconView>(icon::kLayers);
    icon_->Box(44.0f).CornerRadius(22.0f).IconSize(18.0f);
    title_ = &Add<Label>(App::Strings().empty_state_default, TextRole::BodyStrong);
    hint_ = &Add<Label>(L"", TextRole::Caption).Secondary(true);
    hint_->Visible(false);
}

EmptyState& EmptyState::Title(std::wstring_view value) {
    title_->Text(value);
    return *this;
}

const std::wstring& EmptyState::Title() const noexcept { return title_->Text(); }

EmptyState& EmptyState::Hint(std::wstring_view value) {
    hint_->Text(value);
    hint_->Visible(!value.empty());
    return *this;
}

const std::wstring& EmptyState::Hint() const noexcept { return hint_->Text(); }

EmptyState& EmptyState::Glyph(std::wstring_view value) {
    if (value.empty()) {
        icon_->Visible(false);
    } else {
        icon_->Glyph(value);
        icon_->Visible(true);
    }
    return *this;
}

EmptyState& EmptyState::Action(std::wstring_view label, std::function<void()> on_click) {
    if (!action_) {
        action_ = &Add<Button>(L"", ButtonKind::Primary);
    }
    action_->Text(std::wstring(label));
    action_->OnClick(std::move(on_click));
    action_->Visible(!label.empty());
    return *this;
}

} // namespace lumen
