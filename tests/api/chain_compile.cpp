// chain_compile.cpp — 10.1 回归：每个默认可构造控件都能链基类 setter。
#include <lumen/lumen.h>

#define LUMEN_CHAIN(T) \
    host.Add<T>().Margin(4.0f).Visible(true).ToolTip(L"").Grow().Enabled(true)

int main() {
    lumen::Column host;
    LUMEN_CHAIN(lumen::Panel);
    LUMEN_CHAIN(lumen::StackPanel);
    LUMEN_CHAIN(lumen::Row);
    LUMEN_CHAIN(lumen::Column);
    LUMEN_CHAIN(lumen::WrapPanel);
    LUMEN_CHAIN(lumen::Grid);
    LUMEN_CHAIN(lumen::ZStack);
    LUMEN_CHAIN(lumen::Spacer);
    LUMEN_CHAIN(lumen::Button);
    LUMEN_CHAIN(lumen::RepeatButton);
    LUMEN_CHAIN(lumen::ToggleButton);
    LUMEN_CHAIN(lumen::DropDownButton);
    LUMEN_CHAIN(lumen::SplitButton);
    LUMEN_CHAIN(lumen::HyperlinkButton);
    LUMEN_CHAIN(lumen::CheckBox);
    LUMEN_CHAIN(lumen::RadioButton);
    LUMEN_CHAIN(lumen::Switch);
    LUMEN_CHAIN(lumen::Label);
    LUMEN_CHAIN(lumen::RichLabel);
    LUMEN_CHAIN(lumen::TextBox);
    LUMEN_CHAIN(lumen::PasswordBox);
    LUMEN_CHAIN(lumen::NumberBox);
    LUMEN_CHAIN(lumen::AutoSuggestBox);
    LUMEN_CHAIN(lumen::HotkeyBox);
    LUMEN_CHAIN(lumen::Slider);
    LUMEN_CHAIN(lumen::RangeSlider);
    LUMEN_CHAIN(lumen::ProgressBar);
    LUMEN_CHAIN(lumen::ProgressRing);
    LUMEN_CHAIN(lumen::Gauge);
    LUMEN_CHAIN(lumen::Sparkline);
    LUMEN_CHAIN(lumen::Chart);
    LUMEN_CHAIN(lumen::ComboBox);
    LUMEN_CHAIN(lumen::ListView);
    LUMEN_CHAIN(lumen::Table);
    LUMEN_CHAIN(lumen::TreeView);
    LUMEN_CHAIN(lumen::TreeTable);
    LUMEN_CHAIN(lumen::GridView);
    LUMEN_CHAIN(lumen::TabControl);
    LUMEN_CHAIN(lumen::Segmented);
    LUMEN_CHAIN(lumen::Chip);
    LUMEN_CHAIN(lumen::TokenBox);
    LUMEN_CHAIN(lumen::Badge);
    LUMEN_CHAIN(lumen::InfoBadge);
    LUMEN_CHAIN(lumen::IconView);
    LUMEN_CHAIN(lumen::Avatar);
    LUMEN_CHAIN(lumen::Rating);
    LUMEN_CHAIN(lumen::ImageView);
    LUMEN_CHAIN(lumen::Skeleton);
    LUMEN_CHAIN(lumen::Separator);
    LUMEN_CHAIN(lumen::Expander);
    LUMEN_CHAIN(lumen::SettingsCard);
    LUMEN_CHAIN(lumen::InfoBar);
    LUMEN_CHAIN(lumen::FormField);
    LUMEN_CHAIN(lumen::Form);
    LUMEN_CHAIN(lumen::GroupBox);
    LUMEN_CHAIN(lumen::ScrollViewer);
    LUMEN_CHAIN(lumen::SplitView);
    LUMEN_CHAIN(lumen::Splitter);
    LUMEN_CHAIN(lumen::Viewbox);
    LUMEN_CHAIN(lumen::Carousel);
    LUMEN_CHAIN(lumen::Stepper);
    LUMEN_CHAIN(lumen::Pagination);
    LUMEN_CHAIN(lumen::DatePicker);
    LUMEN_CHAIN(lumen::TimePicker);
    LUMEN_CHAIN(lumen::CalendarView);
    LUMEN_CHAIN(lumen::ColorPicker);
    LUMEN_CHAIN(lumen::FileDropZone);
    LUMEN_CHAIN(lumen::NavigationView);
    LUMEN_CHAIN(lumen::PageHost);
    LUMEN_CHAIN(lumen::Breadcrumb);
    LUMEN_CHAIN(lumen::MenuBar);
    LUMEN_CHAIN(lumen::CommandBar);
    LUMEN_CHAIN(lumen::StatusBar);
    LUMEN_CHAIN(lumen::TitleBar);
    LUMEN_CHAIN(lumen::LogView);
    LUMEN_CHAIN(lumen::Dialog);
    LUMEN_CHAIN(lumen::Flyout);
    LUMEN_CHAIN(lumen::TeachingTip);
    LUMEN_CHAIN(lumen::ToolTip);
    LUMEN_CHAIN(lumen::Drawer);
    LUMEN_CHAIN(lumen::BusyOverlay);
    LUMEN_CHAIN(lumen::EmptyState);
    // ColorSwatch 无默认构造，不进此链。
    (void)host;
    return 0;
}
