#include "common.h"
#include <string>

namespace gallery {

void BuildSelection(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Selection",
             L"Discrete choices: boxes, radios, switches, stars, segments, chips.");

    auto& boxes = Sample(column, L"CheckBox", L"Two-state and three-state.");
    auto& box_row = boxes.Add<Row>().Spacing(20.0f).AlignCross(Cross::Center);
    box_row.Add<CheckBox>(L"Notifications").Checked(true).OnToggled([&window](bool) {
        window.ShowToast(L"Notifications toggled");
    });
    auto& mixed = box_row.Add<CheckBox>(L"Select all");
    mixed.ThreeState(true).State(CheckState::Indeterminate).OnToggled([&window, &mixed](bool) {
        const wchar_t* label = L"Unchecked";
        if (mixed.State() == CheckState::Checked) label = L"Checked";
        else if (mixed.State() == CheckState::Indeterminate) label = L"Indeterminate";
        window.ShowToast(std::wstring(L"Select all · ") + label);
    });
    box_row.Add<CheckBox>(L"Disabled").Enabled(false);

    auto& radios = Sample(column, L"RadioButton", L"One group. Glow presets also live on Overview.");
    auto& radio_row = radios.Add<Row>().Spacing(20.0f).AlignCross(Cross::Center);
    radio_row.Add<RadioButton>(L"Design").Group(2).Checked(true);
    radio_row.Add<RadioButton>(L"Code").Group(2);
    radio_row.Add<RadioButton>(L"Review").Group(2);
    radio_row.Add<RadioButton>(L"Disabled").Group(2).Enabled(false);

    auto& switches = Sample(column, L"Switch", L"On / off. SettingsCard hosts one on Layout.");
    auto& sw_row = switches.Add<Row>().Spacing(16.0f).AlignCross(Cross::Center);
    auto& live = sw_row.Add<Switch>();
    live.Checked(true).OnToggled([&window, &live](bool) {
        window.ShowToast(live.Checked() ? L"On" : L"Off");
    });
    sw_row.Add<Switch>().Enabled(false);

    auto& rate = Sample(column, L"Rating", L"Half-star fill. Read-only for display.");
    auto& rate_row = rate.Add<Row>().Spacing(16.0f).AlignCross(Cross::Center);
    auto& rate_col = rate_row.Add<Column>().Spacing(6.0f);
    auto& rate_meta = rate_col.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& rate_label = rate_meta.Add<Label>(L"3.5 / 5", TextRole::CaptionStrong);
    auto& rate_badge = rate_meta.Add<Badge>(L"3.5", Badge::BadgeTone::Warning);
    rate_col.Add<Rating>()
        .Value(3.5)
        .OnRated([&rate_label, &rate_badge](int stars) {
            rate_label.Text(std::to_wstring(stars) + L" / 5");
            rate_badge.Text(std::to_wstring(stars));
            if (stars >= 4) rate_badge.Tone(Badge::BadgeTone::Success);
            else if (stars <= 2) rate_badge.Tone(Badge::BadgeTone::Neutral);
            else rate_badge.Tone(Badge::BadgeTone::Warning);
        });
    auto& ro = rate_row.Add<Column>().Spacing(6.0f);
    ro.Add<Label>(L"ReadOnly", TextRole::Caption).Secondary(true);
    ro.Add<Rating>().Value(3.5).ReadOnly(true);

    auto& segmented = Sample(column, L"Segmented", L"Exclusive tabs in a compact row.");
    auto& tabs = segmented.Add<Segmented>();
    tabs.AddItem(L"Design");
    tabs.AddItem(L"Code");
    tabs.AddItem(L"Settings");
    tabs.SelectedIndex(0);
    auto& p_design =
        segmented.Add<Label>(L"Design · glow tokens scale with GlowIntensity.", TextRole::Caption);
    p_design.Secondary(true).Wrap(true);
    auto& p_code =
        segmented.Add<Label>(L"Code · #include <lumen/lumen.h> then Add<Stepper>().", TextRole::Caption);
    p_code.Secondary(true).Wrap(true).Visible(false);
    auto& p_settings =
        segmented.Add<Label>(L"Settings · glow intensity lives on Overview.", TextRole::Caption);
    p_settings.Secondary(true).Wrap(true).Visible(false);
    tabs.OnSelectionChanged([&tabs, &p_design, &p_code, &p_settings](ptrdiff_t, ptrdiff_t) {
        const ptrdiff_t i = tabs.SelectedIndex();
        p_design.Visible(i == 0);
        p_code.Visible(i == 1);
        p_settings.Visible(i == 2);
    });

    auto& chips = Sample(column, L"Chip", L"Selectable filter chips and a closable draft.");
    auto& wrap = chips.Add<WrapPanel>().Gap(8.0f, 8.0f);
    const wchar_t* filters[] = {L"Source", L"Controls", L"Layout", L"Tokens"};
    for (int i = 0; i < 4; ++i) {
        auto& chip = wrap.Add<Chip>(filters[i]);
        chip.Selectable(true).Selected(i == 0).Glyph(
            i == 0 ? icon::kCode : (i == 1 ? icon::kLayers : (i == 2 ? icon::kGrid : icon::kTag)));
        chip.OnToggled([&window, &chip](bool) {
            window.ShowToast(chip.Selected() ? chip.Text() + L" on" : chip.Text() + L" off");
        });
    }
    auto& draft = wrap.Add<Chip>(L"Draft");
    draft.Closable(true).OnClosed([&draft, &window] {
        draft.Visible(false);
        window.ShowToast(L"Draft closed");
    });
}

} // namespace gallery
