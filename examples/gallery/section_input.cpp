#include "common.h"
#include <chrono>
#include <cwctype>
#include <string>
#include <vector>

namespace gallery {

void BuildInput(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    using namespace std::chrono;
    PageHead(column, L"Input", L"Fill in a value: text, numbers, dates, color, files, sliders.");

    auto& text = Sample(
        column, L"TextBox",
        L"Double-click a word · triple-click a line · Ctrl+Backspace · Ctrl+Z · right-click. "
        L"Drag a selection onto another field.");
    auto& search_field =
        text.Add<FormField>(L"Search").Description(L"IME composition stays inside the field.");
    search_field.Add<TextBox>()
        .Placeholder(L"Search notes")
        .Glyph(icon::kSearch)
        .Text(L"你好");
    auto& title_row = text.Add<Row>().Spacing(8.0f);
    title_row.Add<TextBox>().Placeholder(L"Title").FloatingLabel(true).Grow();
    title_row.Add<TextBox>().Placeholder(L"Folder").FloatingLabel(true).Grow();
    auto& notes_field = text.Add<FormField>(L"Notes");
    notes_field.Add<TextBox>()
        .Multiline(true)
        .Placeholder(L"Write a note")
        .Text(L"Ship glow tokens this week.\n"
              L"Double-click any word on this line.\n"
              L"Triple-click selects the whole line. Ctrl+Backspace deletes a word.")
        .OnSubmit([&window] { window.ShowToast(L"Note saved"); });
    auto& share = text.Add<Row>().Spacing(8.0f).AlignCross(Cross::Start);
    auto& phone_field = share.Add<FormField>(L"Phone").Description(L"Mask 000-0000").Grow();
    phone_field.Add<TextBox>().Placeholder(L"000-0000").Mask(L"000-0000");
    auto& handle_field = share.Add<FormField>(L"Handle").Description(L"0 / 12").Grow();
    auto& handle = handle_field.Add<TextBox>();
    handle.Placeholder(L"studio")
        .MaxLength(12)
        .OnTextChanged([&handle, &handle_field](std::wstring_view) {
            handle_field.Description(std::to_wstring(handle.Text().size()) + L" / 12");
        });
    auto& clip = text.Add<Row>().Spacing(8.0f);
    clip.Add<TextBox>().Text(L"Select this phrase, then drag it.").Grow();
    clip.Add<TextBox>().Placeholder(L"Drop here").Grow();
    Wide(text).Add<TextBox>().Text(L"LUMEN-0142").Enabled(false).Grow();

    auto& secret = Sample(column, L"PasswordBox", L"Revealable — value is never logged.");
    Wide(secret).Add<PasswordBox>()
        .Revealable(true)
        .Placeholder(L"API token")
        .OnSubmit([&window] { window.ShowToast(L"Token saved"); })
        .Grow();

    auto& number = Sample(column, L"NumberBox", L"Filter, step, clamp on blur. Spin buttons repeat.");
    auto& num_row = number.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    num_row.Add<NumberBox>().Range(0.0, 10.0).Value(6.5).Decimals(1).Step(0.5);
    num_row.Add<NumberBox>().Range(1.0, 99.0).Value(16.0).Decimals(0).Step(1.0);
    num_row.Add<NumberBox>().Range(0.0, 100.0).Value(50.0).Enabled(false);

    auto& suggest_card =
        Sample(column, L"AutoSuggestBox", L"Type to filter. Down arrow opens all suggestions.");
    auto& suggest = Wide(suggest_card).Add<AutoSuggestBox>();
    g_suggest = &suggest;
    suggest.Suggestions([](std::wstring_view query) -> std::vector<std::wstring> {
        static const std::wstring_view kAll[] = {L"Button",   L"CheckBox", L"ComboBox",
                                                 L"TreeView", L"Table",    L"Dialog",
                                                 L"Expander", L"Flyout",   L"Hyperlink",
                                                 L"InfoBar"};
        std::wstring needle(query);
        for (wchar_t& ch : needle) ch = towlower(ch);
        std::vector<std::wstring> out;
        for (const std::wstring_view candidate : kAll) {
            std::wstring hay(candidate);
            for (wchar_t& ch : hay) ch = towlower(ch);
            if (needle.empty() || hay.find(needle) != std::wstring::npos) {
                out.emplace_back(candidate);
            }
        }
        return out;
    });
    suggest.OnSuggestionChosen([](std::wstring_view pick) {
        if (pick == L"Button" || pick == L"Hyperlink") ShowPage(L"buttons");
        else if (pick == L"TreeView" || pick == L"Table") ShowPage(L"collections");
        else if (pick == L"Dialog" || pick == L"Flyout") ShowPage(L"overlays");
        else if (pick == L"Expander") ShowPage(L"layout");
        else if (pick == L"InfoBar") ShowPage(L"status");
        else if (pick == L"CheckBox") ShowPage(L"selection");
        else ShowPage(L"input");
    });
    suggest.Placeholder(L"Jump to control...").Grow();

    auto& combo = Sample(
        column, L"ComboBox",
        L"Virtualized dropdown. Type a letter to jump when closed. Groups. MultiSelect shows Chips.");

    combo.Add<Label>(L"2,000 items · Editable filter", TextRole::Caption).Secondary(true);
    auto& select = Wide(combo).Add<ComboBox>();
    std::vector<std::wstring> projects;
    projects.reserve(2000);
    for (size_t i = 0; i < 2000; ++i) {
        projects.push_back(L"Lumen workspace " + std::to_wstring(i + 1));
    }
    select.Items(std::move(projects))
        .MaxDropDownRows(8)
        .SelectedIndex(31)
        .Editable(true)
        .OnSelectionChanged([&window, &select](ptrdiff_t, ptrdiff_t) {
            window.ShowToast(L"SelectedText · " + select.SelectedText());
        })
        .Grow();

    combo.Add<Label>(L"Grouped · type-ahead (not editable)", TextRole::Caption).Secondary(true);
    auto& grouped = Wide(combo).Add<ComboBox>();
    grouped.AddItems({L"North · Aurora", L"North · Borealis", L"West · Harbor", L"West · Dune",
                      L"Core · Lumen", L"Core · Void"})
        .Groups({{L"n", L"North", 2}, {L"w", L"West", 2}, {L"c", L"Core", 2}})
        .SelectedIndex(0)
        .Placeholder(L"Region")
        .OnSelectionChanged([&window, &grouped](ptrdiff_t, ptrdiff_t) {
            window.ShowToast(L"Region · " + grouped.SelectedText());
        })
        .Grow();

    combo.Add<Label>(L"MultiSelect · chips, click × to remove", TextRole::Caption).Secondary(true);
    auto& multi = Wide(combo).Add<ComboBox>();
    multi.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient", L"Carbon", L"Void"})
        .MultiSelect(true)
        .SelectedIndices({0, 2, 4})
        .Placeholder(L"Tokens")
        .OnSelectionChanged([&window, &multi](ptrdiff_t, ptrdiff_t) {
            window.ShowToast(L"Selected · " + std::to_wstring(multi.SelectionCount()));
        })
        .Grow();

    auto& hotkey_card = Sample(column, L"HotkeyBox", L"Focus, press a chord. Backspace / × clears.");
    auto& hotkey_row = hotkey_card.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    auto& hotkey = hotkey_row.Add<HotkeyBox>();
    hotkey.Chord(L"Ctrl+K").Grow();
    auto& hotkey_shown = hotkey_row.Add<Label>(L"Ctrl+K", TextRole::CaptionStrong);
    hotkey.OnChanged([&window, &hotkey, &hotkey_shown] {
        const std::wstring chord = hotkey.Empty() ? L"None" : hotkey.Chord();
        hotkey_shown.Text(chord);
        window.ShowToast(chord);
    });

    auto& tokens = Sample(column, L"TokenBox", L"Enter / comma commits. Backspace deletes the last token.");
    auto& tag_head = tokens.Add<Row>().AlignCross(Cross::Center);
    tag_head.Add<Label>(L"Tags", TextRole::Caption).Secondary(true);
    tag_head.Add<Spacer>();
    auto& tag_count = tag_head.Add<Label>(L"2 tags", TextRole::CaptionStrong);
    auto& tags = tokens.Add<TokenBox>();
    tags.Placeholder(L"Add tag")
        .Tokens({L"Source", L"Glow"})
        .OnTokensChanged([&tag_count, &tags] {
            tag_count.Text(std::to_wstring(tags.Tokens().size()) + L" tags");
        });

    auto& form = Sample(column, L"Form", L"Field + Validate + BindText. Submit follows Valid().");
    static Property<std::wstring> g_project;
    static Property<std::wstring> g_mail;
    auto& sheet = form.Add<Form>();
    sheet.Field(L"Project")
        .Required(true)
        .Validate(validate::Required())
        .Add<TextBox>()
        .Placeholder(L"Display name")
        .BindText(g_project);
    sheet.Field(L"Notify")
        .Required(true)
        .Validate(validate::Required() | validate::Pattern(L".+@.+"))
        .Add<TextBox>()
        .Placeholder(L"studio@lumen")
        .BindText(g_mail);
    sheet.Add<Button>(L"Submit", ButtonKind::Primary).BindEnabled(sheet.Valid());

    auto& sliders = Sample(column, L"Slider / RangeSlider", L"Horizontal, vertical, and a range.");
    auto& slider_modes = sliders.Add<Row>().Spacing(24.0f).AlignCross(Cross::Center);
    auto& range_column = slider_modes.Add<Column>().Spacing(8.0f).Grow();
    range_column.Add<Label>(L"RangeSlider · window", TextRole::Caption).Secondary(true);
    auto& range = range_column.Add<RangeSlider>();
    range.Range(0.0f, 24.0f)
        .Values(9.0f, 18.0f)
        .Step(1.0f)
        .OnValueChanged([&window](float lower, float upper) {
            window.ShowToast(L"Window · " + std::to_wstring(static_cast<int>(lower)) + L":00–" +
                             std::to_wstring(static_cast<int>(upper)) + L":00");
        });
    auto& vertical_column = slider_modes.Add<Column>().Spacing(8.0f).AlignCross(Cross::Center);
    vertical_column.Add<Label>(L"Vertical", TextRole::Caption).Secondary(true);
    vertical_column.Add<Slider>()
        .Orientation(SliderOrientation::Vertical)
        .Range(0.0f, 100.0f)
        .Value(65.0f)
        .Step(5.0f);
    Wide(sliders).Add<Slider>().Range(0.0f, 100.0f).Value(40.0f).Enabled(false).Grow();

    auto& dates = Sample(column, L"Date / Time / Calendar", L"std::chrono values. CalendarView shares the DatePicker layer.");
    auto& pickers = dates.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    pickers.Add<DatePicker>()
        .Value(year{2026} / September / day{1})
        .OnValueChanged([&window](std::optional<DatePicker::Date>) {
            window.ShowToast(L"Date updated");
        });
    pickers.Add<TimePicker>()
        .Value(hours{9} + minutes{30})
        .DisplayMode(TimeDisplayMode::TwelveHour)
        .OnValueChanged([&window](std::optional<TimePicker::Time>) {
            window.ShowToast(L"Time updated");
        });
    dates.Add<CalendarView>()
        .Value(year{2026} / September / day{1})
        .OnDateChanged([&window](std::optional<CalendarView::Date> value) {
            if (value) window.ShowToast(L"CalendarView date");
        });

    auto& color = Sample(column, L"ColorPicker", L"HSV + hex. Copy or Ctrl+C.");
    color.Add<ColorPicker>().Color(Color::Hex(0xFFFFFF));

    auto& drop_card =
        Sample(column, L"FileDropZone", L"Drag from Explorer (OLE CF_HDROP).");
    auto& drop_status = drop_card.Add<Label>(L"No files yet", TextRole::Caption).Secondary(true);
    drop_card.Add<FileDropZone>()
        .Hint(L"Images, source, anything Explorer can hand over")
        .OnDrop([&window, &drop_status](const std::vector<std::wstring>& paths) {
            std::wstring name = paths.front();
            const size_t slash = name.find_last_of(L"\\/");
            if (slash != std::wstring::npos) name = name.substr(slash + 1);
            drop_status.Text(std::to_wstring(paths.size()) + L" · " + name);
            window.ShowToast(L"Dropped " + name);
        });

    auto& rich = Sample(column, L"RichLabel", L"Inline strong / secondary / link, wraps on the card width.");
    Wide(rich).Add<RichLabel>()
        .Add(L"Used ")
        .Strong(L"85%")
        .Add(L" of the void. ")
        .Secondary(L"Last compact 2h ago. ")
        .Link(L"Reclaim space", [&window] { window.ShowToast(L"Reclaim"); })
        .Grow();
}

} // namespace gallery
