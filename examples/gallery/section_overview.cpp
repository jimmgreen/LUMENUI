#include "common.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cwchar>
#include <string>
#include <vector>

namespace gallery {
namespace {

struct IconSpec {
    const wchar_t* glyph;
    const wchar_t* name;
};

constexpr IconSpec kWindowIcons[] = {
    {lumen::icon::kClose, L"Close"},
    {lumen::icon::kMinimize, L"Minimize"},
    {lumen::icon::kMaximize, L"Maximize"},
    {lumen::icon::kRestore, L"Restore"},
};
constexpr IconSpec kEditIcons[] = {
    {lumen::icon::kAdd, L"Add"},
    {lumen::icon::kRemove, L"Remove"},
    {lumen::icon::kDelete, L"Delete"},
    {lumen::icon::kEdit, L"Edit"},
    {lumen::icon::kCut, L"Cut"},
    {lumen::icon::kCopy, L"Copy"},
    {lumen::icon::kPaste, L"Paste"},
    {lumen::icon::kUndo, L"Undo"},
    {lumen::icon::kRedo, L"Redo"},
    {lumen::icon::kSave, L"Save"},
};
constexpr IconSpec kFindIcons[] = {
    {lumen::icon::kSearch, L"Search"},
    {lumen::icon::kFilter, L"Filter"},
    {lumen::icon::kSort, L"Sort"},
    {lumen::icon::kView, L"View"},
    {lumen::icon::kHide, L"Hide"},
    {lumen::icon::kMenu, L"Menu"},
    {lumen::icon::kList, L"List"},
    {lumen::icon::kRows, L"Rows"},
    {lumen::icon::kMore, L"More"},
    {lumen::icon::kGrid, L"Grid"},
    {lumen::icon::kSliders, L"Sliders"},
};
constexpr IconSpec kFileIcons[] = {
    {lumen::icon::kFolder, L"Folder"},
    {lumen::icon::kFolderOpen, L"FolderOpen"},
    {lumen::icon::kFile, L"File"},
    {lumen::icon::kImage, L"Image"},
    {lumen::icon::kDownload, L"Download"},
    {lumen::icon::kUpload, L"Upload"},
    {lumen::icon::kShare, L"Share"},
    {lumen::icon::kLink, L"Link"},
    {lumen::icon::kAttach, L"Attach"},
    {lumen::icon::kExternalLink, L"ExternalLink"},
    {lumen::icon::kPrint, L"Print"},
};
constexpr IconSpec kNavIcons[] = {
    {lumen::icon::kHome, L"Home"},
    {lumen::icon::kChevronLeft, L"ChevronLeft"},
    {lumen::icon::kChevronRight, L"ChevronRight"},
    {lumen::icon::kChevronUp, L"ChevronUp"},
    {lumen::icon::kChevronDown, L"ChevronDown"},
    {lumen::icon::kArrowLeft, L"ArrowLeft"},
    {lumen::icon::kArrowRight, L"ArrowRight"},
    {lumen::icon::kArrowUp, L"ArrowUp"},
    {lumen::icon::kArrowDown, L"ArrowDown"},
    {lumen::icon::kRefresh, L"Refresh"},
    {lumen::icon::kSignOut, L"SignOut"},
};
constexpr IconSpec kCommsIcons[] = {
    {lumen::icon::kMail, L"Mail"},
    {lumen::icon::kInbox, L"Inbox"},
    {lumen::icon::kSend, L"Send"},
    {lumen::icon::kChat, L"Chat"},
    {lumen::icon::kBell, L"Bell"},
    {lumen::icon::kContact, L"Contact"},
    {lumen::icon::kPeople, L"People"},
};
constexpr IconSpec kMediaIcons[] = {
    {lumen::icon::kPlay, L"Play"},
    {lumen::icon::kPause, L"Pause"},
    {lumen::icon::kVolume, L"Volume"},
    {lumen::icon::kCamera, L"Camera"},
    {lumen::icon::kVideo, L"Video"},
};
constexpr IconSpec kStatusIcons[] = {
    {lumen::icon::kCheckMark, L"CheckMark"},
    {lumen::icon::kCheckSquare, L"CheckSquare"},
    {lumen::icon::kWarning, L"Warning"},
    {lumen::icon::kInfo, L"Info"},
    {lumen::icon::kHelp, L"Help"},
    {lumen::icon::kShield, L"Shield"},
    {lumen::icon::kFavorite, L"Favorite"},
    {lumen::icon::kFavoriteFill, L"FavoriteFill"},
    {lumen::icon::kHeart, L"Heart"},
    {lumen::icon::kPin, L"Pin"},
    {lumen::icon::kFlag, L"Flag"},
    {lumen::icon::kTag, L"Tag"},
    {lumen::icon::kBookmark, L"Bookmark"},
    {lumen::icon::kSparkle, L"Sparkle"},
    {lumen::icon::kZap, L"Zap"},
};
constexpr IconSpec kSystemIcons[] = {
    {lumen::icon::kSettings, L"Settings"},
    {lumen::icon::kLock, L"Lock"},
    {lumen::icon::kUnlock, L"Unlock"},
    {lumen::icon::kGlobe, L"Globe"},
    {lumen::icon::kCloud, L"Cloud"},
    {lumen::icon::kWifi, L"Wifi"},
    {lumen::icon::kPower, L"Power"},
    {lumen::icon::kSun, L"Sun"},
    {lumen::icon::kMoon, L"Moon"},
    {lumen::icon::kKeyboard, L"Keyboard"},
    {lumen::icon::kCalendar, L"Calendar"},
    {lumen::icon::kClock, L"Clock"},
    {lumen::icon::kLocation, L"Location"},
};
constexpr IconSpec kDevIcons[] = {
    {lumen::icon::kCode, L"Code"},
    {lumen::icon::kTerminal, L"Terminal"},
    {lumen::icon::kDatabase, L"Database"},
    {lumen::icon::kBug, L"Bug"},
    {lumen::icon::kChart, L"Chart"},
    {lumen::icon::kPackage, L"Package"},
    {lumen::icon::kLayers, L"Layers"},
    {lumen::icon::kCards, L"Cards"},
};

constexpr char kCirclePath[] =
    "M128,24A104,104,0,1,0,232,128,104.11,104.11,0,0,0,128,24Zm0,192a88,88,0,1,1,88-88A88.1,88.1,0,0,1,128,216Z";
constexpr wchar_t kCircleGlyph[] = L"\uF000";

struct CircleReg {
    CircleReg() { lumen::icon::Register(kCircleGlyph[0], kCirclePath); }
};
const CircleReg g_circle_reg;

class PainterIconStrip : public lumen::Control {
protected:
    lumen::Size Measure(lumen::Size, const lumen::Theme&) override { return {120.0f, 28.0f}; }
    void Draw(lumen::Painter& painter, const lumen::Theme& theme) override {
        using namespace lumen;
        const float s = icon::kSize;
        float x = absolute_.x;
        const float y = absolute_.y + (absolute_.h - s) * 0.5f;
        painter.DrawIcon(icon::kSearch, {x, y, s, s}, theme.text);
        x += s + 12.0f;
        painter.DrawIcon(icon::kZap, Point{x + s * 0.5f, absolute_.y + absolute_.h * 0.5f},
                         theme.text);
        x += s + 12.0f;
        painter.DrawIconPath(kCirclePath, {x, y, s, s}, theme.text);
    }
    bool HitTransparent() const noexcept override { return true; }
};

struct IconPlayground {
    lumen::Slider* size_slider = nullptr;
    lumen::Slider* weight_slider = nullptr;
    lumen::NumberBox* size_box = nullptr;
    lumen::NumberBox* weight_box = nullptr;
    lumen::Label* size_label = nullptr;
    lumen::Label* weight_label = nullptr;
    lumen::IconView* preview = nullptr;
    std::vector<lumen::IconView*> icons;
    bool syncing = false;
};

IconPlayground g_icon_play;

void ApplyIconPlay(float size, float weight) {
    using namespace lumen;
    size = Clamp(size, 16.0f, 72.0f);
    weight = Clamp(weight, 1.0f, 2.0f);
    weight = std::round(weight * 10.0f) / 10.0f;
    wchar_t buf[32];
    swprintf_s(buf, L"%d px", static_cast<int>(size + 0.5f));
    if (g_icon_play.size_label) g_icon_play.size_label->Text(buf);
    swprintf_s(buf, L"%.1f", static_cast<double>(weight));
    if (g_icon_play.weight_label) g_icon_play.weight_label->Text(buf);

    if (g_icon_play.syncing) return;
    g_icon_play.syncing = true;
    if (g_icon_play.size_slider) g_icon_play.size_slider->Value(size);
    if (g_icon_play.size_box) g_icon_play.size_box->Value(static_cast<double>(size));
    if (g_icon_play.weight_slider) g_icon_play.weight_slider->Value(weight);
    if (g_icon_play.weight_box) g_icon_play.weight_box->Value(static_cast<double>(weight));
    g_icon_play.syncing = false;

    const float box = size + 12.0f;
    for (IconView* icon : g_icon_play.icons) {
        if (!icon) continue;
        icon->Box(box).IconSize(size).Weight(weight);
    }
    if (g_icon_play.preview) {
        g_icon_play.preview->Box(76.0f).IconSize(64.0f).Weight(weight);
    }
}

struct SaveDemo {
    bool dirty = true;
    lumen::Window* window = nullptr;
    lumen::Switch* dirty_switch = nullptr;
    lumen::Label* status = nullptr;
    lumen::Command save;
    SaveDemo()
        : save(L"Save", lumen::icon::kSave, L"Ctrl+Shift+S", [this] {
              dirty = false;
              save.RaiseCanExecuteChanged();
              if (dirty_switch) dirty_switch->Checked(false);
              if (status) status->Text(L"Saved");
              if (window) window->ShowToast(L"Saved");
          }) {
        save.CanExecute([this] { return dirty; });
    }
};
SaveDemo g_save;

} // namespace

void BuildOverview(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Overview",
             L"Native Windows UI with a monochrome glow. Categories on the left; "
             L"each page shows related controls and their states.");

    auto& hero = Sample(column, L"LUMEN", L"Design with Pure Light.");
    hero.Add<Chip>(L"LUMEN v1.0 is now available").Glyph(icon::kSparkle);
    hero.Add<Label>(L"A native Windows component library focused on high-contrast, glowing aesthetics.",
                    TextRole::Body)
        .Secondary(true)
        .Wrap(true);
    auto& jumps = hero.Add<WrapPanel>().Gap(8.0f, 8.0f);
    struct Jump {
        const wchar_t* id;
        const wchar_t* label;
    };
    for (const Jump j : {Jump{L"buttons", L"Buttons"}, Jump{L"input", L"Input"},
                         Jump{L"layout", L"Layout"}, Jump{L"collections", L"Collections"},
                         Jump{L"overlays", L"Overlays"}, Jump{L"status", L"Status"}}) {
        jumps.Add<Button>(j.label, ButtonKind::Subtle)
            .SizeClass(ButtonSize::Small)
            .OnClick([id = j.id] { ShowPage(id); });
    }

    g_save.window = &window;
    static bool save_bound = false;
    if (!save_bound) {
        window.Bind(g_save.save);
        save_bound = true;
    }

    auto& decl = Sample(column, L"Declarative Children",
                        L"Column().Children / FormField.Child / Button.Ref — nested structure without Add<>.");
    Button* ok = nullptr;
    decl.Children(
        Column().Comfortable().Children(
            FormField(L"Name").Child(TextBox().Placeholder(L"Project name")),
            Row().Spacing(8.0f).Children(
                Button(L"Cancel"),
                Button(L"OK", ButtonKind::Primary).Ref(ok))));
    if (ok) {
        ok->OnClick([&window] { window.ShowToast(L"OK"); });
    }

    auto& cmd = Sample(column, L"Command / Density",
                       L"Command shares CanExecute across Button, CommandBar, and Ctrl+Shift+S. Compact subtree shortens buttons.");
    auto& cmd_col = cmd.Add<Column>().Spacing(12.0f);
    cmd_col.Children(
        Row().Spacing(12.0f).AlignCross(Cross::Center).Children(
            Switch(L"Unsaved changes")
                .Checked(true)
                .Ref(g_save.dirty_switch)
                .OnToggled([](bool on) {
                    g_save.dirty = on;
                    g_save.save.RaiseCanExecuteChanged();
                    if (g_save.status) g_save.status->Text(on ? L"Dirty" : L"Clean");
                }),
            Button().Bind(g_save.save),
            Label(L"Dirty", TextRole::Caption).Secondary(true).Ref(g_save.status)));
    cmd_col.Add<CommandBar>().Add(g_save.save);
    auto& compact = cmd_col.Add<Column>().Spacing(8.0f);
    compact.Density(Density::Compact);
    compact.Children(
        Label(L"Compact density", TextRole::Caption).Secondary(true),
        Row().Spacing(8.0f).Children(Button(L"Compact"),
                                     Button(L"Also compact", ButtonKind::Primary)));

    auto& present = Sample(column, L"Dirty present",
                           L"Hover a button: title-bar HUD switches from full to N dirty (Present1). Resize the window to force a full present.");
    present.Children(
        Row().Spacing(8.0f).Children(Button(L"Hover me"), Button(L"Or me", ButtonKind::Primary)));

    auto& glow = Sample(column, L"Glow intensity",
                        L"ColorSwatch / Slider / NumberBox all call Window::GlowIntensity.");
    auto& swatches = glow.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    const Color fills[5] = {
        Color::Hex(0x1A1A1A), Color::Hex(0x595959), Color::Hex(0x808080),
        Color::Hex(0xBFBFBF), Color::Hex(0xFFFFFF),
    };
    const wchar_t* tips[5] = {L"0%", L"35%", L"50%", L"75%", L"100%"};
    for (int n = 0; n < 5; ++n) {
        const float stop = kSwatchStops[n];
        auto& sw = swatches.Add<ColorSwatch>(fills[n]);
        sw.ToolTip(tips[n])
            .BindSelected(g_glow, [n](float v) {
                size_t best = 0;
                float best_d = 2.0f;
                for (size_t i = 0; i < 5; ++i) {
                    const float d = std::fabs(kSwatchStops[i] - v);
                    if (d < best_d) {
                        best_d = d;
                        best = i;
                    }
                }
                return best == static_cast<size_t>(n);
            })
            .OnPicked([&window, stop] { SetIntensity(window, stop); });
    }
    auto& meter_head = glow.Add<Row>().AlignCross(Cross::Center);
    meter_head.Add<Label>(L"GlowIntensity", TextRole::Caption).Secondary(true);
    meter_head.Add<Spacer>();
    meter_head.Add<Label>(L"50%", TextRole::CaptionStrong)
        .BindText(g_glow, [](float v) {
            wchar_t buf[16];
            swprintf_s(buf, L"%d%%", static_cast<int>(v * 100.0f + 0.5f));
            return std::wstring(buf);
        });
    auto& numeric = glow.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    numeric.Add<Label>(L"光效强度", TextRole::Caption).Secondary(true);
    auto& box = numeric.Add<NumberBox>();
    box.Range(0.0, 100.0)
        .Step(5.0)
        .BindValue(g_glow, 100.0f);
    auto& slider = Wide(glow).Add<Slider>();
    slider.Grow();
    slider.Range(0.0f, 100.0f)
        .Step(1.0f)
        .BindValue(g_glow, 100.0f);
    auto& light = glow.Add<SettingsCard>();
    light.Title(L"Dynamic Light")
        .Description(L"Off mutes glow to 0; on restores the last intensity.")
        .Glyph(icon::kSparkle);
    auto& dyn = light.Add<Switch>();
    g_dynamic_light = &dyn;
    dyn.BindChecked(g_glow, [](float v) { return v > 0.01f; })
        .OnToggled([&window](bool) {
            if (!g_dynamic_light) return;
            SetIntensity(window, g_dynamic_light->Checked() ? LastGlow() : 0.0f);
        });
    auto& radios = glow.Add<Row>().Spacing(20.0f).AlignCross(Cross::Center);
    auto& preset_void = radios.Add<RadioButton>(L"Void").Group(1);
    preset_void.BindChecked(g_glow, [](float v) { return v <= 0.02f; })
        .OnToggled([&window](bool on) {
            if (on) SetIntensity(window, 0.0f);
        });
    auto& preset_soft = radios.Add<RadioButton>(L"Soft").Group(1);
    preset_soft.BindChecked(g_glow, [](float v) { return std::fabs(v - 0.5f) < 0.05f; })
        .OnToggled([&window](bool on) {
            if (on) SetIntensity(window, 0.5f);
        });
    auto& preset_full = radios.Add<RadioButton>(L"Full").Group(1);
    preset_full.BindChecked(g_glow, [](float v) { return v >= 0.98f; })
        .OnToggled([&window](bool on) {
            if (on) SetIntensity(window, 1.0f);
        });

    auto& tokens = Sample(column, L"Glow tokens",
                          L"All light tokens scale with glow_intensity. Spotlight is Lumen cards only.");
    auto& expander = tokens.Add<Expander>(L"glow_sm / md / lg · spotlight · specular · ambient");
    expander.Add<Label>(L"Sample cards use CardStyle::Lumen. Inner controls do not self-ignite.",
                        TextRole::Caption)
        .Secondary(true)
        .Wrap(true);
    expander.Expanded(true);
    Menu token_menu;
    token_menu.AddItem(L"Copy tokens", [&window] { window.ShowToast(L"Copied glow tokens"); });
    token_menu.AddItem(L"Reset intensity", [&window] { SetIntensity(window, 0.5f); });
    expander.ContextMenu(std::move(token_menu));

    auto& avatars = Sample(column, L"Avatar", L"Initials, glyph, presence.");
    auto& identity = avatars.Add<Row>().AlignCross(Cross::Center);
    auto& profile = identity.Add<Row>();
    profile.Card(Panel::CardStyle::Subtle, 20.0f);
    profile.Padding(6.0f, 4.0f).Spacing(10.0f).AlignCross(Cross::Center);
    profile.Add<Avatar>(L"林").Diameter(32.0f).PresenceState(Avatar::Presence::Online);
    auto& who = profile.Add<Column>().Spacing(1.0f);
    who.Add<Label>(L"Admin", TextRole::CaptionStrong);
    who.Add<Label>(L"Level 9", TextRole::Caption).Secondary(true);
    identity.Add<Spacer>();
    auto& faces = identity.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    faces.Add<Avatar>(L"Alice").Diameter(26.0f).PresenceState(Avatar::Presence::Away);
    faces.Add<Avatar>().Glyph(icon::kContact).Diameter(26.0f).PresenceState(Avatar::Presence::Busy);

    auto& images = Sample(column, L"ImageView", L"Same 16:9 bitmap, three stretch modes.");
    const auto preview = MakeSceneBitmap();
    auto& tiles = images.Add<Row>().Spacing(12.0f).AlignCross(Cross::Start);
    struct ImageDemo {
        ImageStretch stretch;
        float radius;
        const wchar_t* title;
        const wchar_t* tip;
    };
    const ImageDemo demos[] = {
        {ImageStretch::Uniform, 0.0f, L"Uniform", L"完整显示，可能留边"},
        {ImageStretch::UniformToFill, 16.0f, L"UniformToFill", L"裁切铺满"},
        {ImageStretch::Fill, 0.0f, L"Fill", L"拉满视口，比例会变"},
    };
    constexpr float kBox = 168.0f;
    for (const ImageDemo& demo : demos) {
        auto& col = tiles.Add<Column>().Spacing(8.0f);
        auto& frame = col.Add<Panel>();
        frame.Card(Panel::CardStyle::Input, 12.0f).Clip(true);
        auto& image = frame.Add<ImageView>();
        image.SetBounds({0.0f, 0.0f, kBox, kBox});
        image.LoadMemory(preview);
        image.Stretch(demo.stretch).CornerRadius(demo.radius).ToolTip(demo.tip);
        col.Add<Label>(demo.title, TextRole::Caption).Secondary(true);
    }
    auto& states = images.Add<Row>().Spacing(12.0f).AlignCross(Cross::Start);
    auto add_state = [&](bool fail, const wchar_t* title) {
        auto& col = states.Add<Column>().Spacing(8.0f);
        auto& frame = col.Add<Panel>();
        frame.Card(Panel::CardStyle::Input, 12.0f).Clip(true);
        auto& image = frame.Add<ImageView>();
        image.SetBounds({0.0f, 0.0f, kBox, kBox});
        image.CornerRadius(12.0f);
        if (fail) {
            const std::array<std::byte, 4> junk{};
            image.LoadMemory(junk);
            image.ErrorPlaceholder(L"Couldn't load", L"Missing or invalid source");
        } else {
            image.Placeholder(L"No image", L"Load a file or bitmap");
        }
        col.Add<Label>(title, TextRole::Caption).Secondary(true);
    };
    add_state(false, L"空 · 尚未指定源");
    add_state(true, L"失败 · 解码被拒绝");

    auto& icons = Sample(column, L"Icons · Phosphor",
                         L"Grouped by use. DrawIcon / DrawIconPath / icon::Register. Size and weight apply to the grid.");
    auto& api = icons.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    api.Add<PainterIconStrip>();
    api.Add<IconView>(kCircleGlyph)
        .Box(28.0f)
        .IconSize(icon::kSize)
        .Weight(icon::kWeight)
        .CornerRadius(8.0f)
        .Background(Color{0.0f, 0.0f, 0.0f, 0.0f})
        .Stroke(Color{0.0f, 0.0f, 0.0f, 0.0f})
        .ToolTip(L"Registered F000 circle");
    api.Add<Label>(L"Register a 256-viewBox SVG d to reuse IconView / Button::Glyph.",
                   TextRole::Caption)
        .Secondary(true)
        .Wrap(true)
        .Grow();

    auto& head = icons.Add<Row>().Spacing(20.0f).AlignCross(Cross::Center);
    auto& preview_icon = head.Add<IconView>(icon::kSparkle)
                             .Box(76.0f)
                             .IconSize(64.0f)
                             .Weight(1.5f)
                             .CornerRadius(16.0f)
                             .Badge(InfoBadgeData::Count(3))
                             .ToolTip(L"Sparkle");
    g_icon_play.preview = &preview_icon;
    auto& meters = head.Add<Column>().Spacing(10.0f).Grow();
    auto& size_meter = meters.Add<Column>().Spacing(6.0f);
    auto& size_head = size_meter.Add<Row>().AlignMain(Main::SpaceBetween).AlignCross(Cross::Center);
    size_head.Add<Label>(L"Size", TextRole::Caption).Secondary(true);
    g_icon_play.size_label = &size_head.Add<Label>(L"16 px", TextRole::CaptionStrong);
    auto& size_row = size_meter.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    auto& size_box = size_row.Add<NumberBox>();
    g_icon_play.size_box = &size_box;
    size_box.Range(16.0, 72.0).Value(16.0).Step(1.0).Decimals(0).OnValueChanged([](double v) {
        if (g_icon_play.syncing) return;
        const float weight = g_icon_play.weight_slider ? g_icon_play.weight_slider->Value() : 1.5f;
        ApplyIconPlay(static_cast<float>(v), weight);
    });
    auto& size_slider = size_meter.Add<Slider>();
    g_icon_play.size_slider = &size_slider;
    size_slider.Range(16.0f, 72.0f).Value(16.0f).OnValueChanged([](float) {
        if (g_icon_play.syncing || !g_icon_play.size_slider) return;
        const float weight = g_icon_play.weight_slider ? g_icon_play.weight_slider->Value() : 1.5f;
        ApplyIconPlay(g_icon_play.size_slider->Value(), weight);
    });
    auto& weight_meter = meters.Add<Column>().Spacing(6.0f);
    auto& weight_head =
        weight_meter.Add<Row>().AlignMain(Main::SpaceBetween).AlignCross(Cross::Center);
    weight_head.Add<Label>(L"Weight", TextRole::Caption).Secondary(true);
    g_icon_play.weight_label = &weight_head.Add<Label>(L"1.5", TextRole::CaptionStrong);
    auto& weight_row = weight_meter.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    auto& weight_box = weight_row.Add<NumberBox>();
    g_icon_play.weight_box = &weight_box;
    weight_box.Range(1.0, 2.0).Value(1.5).Step(0.1).Decimals(1).OnValueChanged([](double v) {
        if (g_icon_play.syncing) return;
        const float size = g_icon_play.size_slider ? g_icon_play.size_slider->Value() : 16.0f;
        ApplyIconPlay(size, static_cast<float>(v));
    });
    auto& weight_slider = weight_meter.Add<Slider>();
    g_icon_play.weight_slider = &weight_slider;
    weight_slider.Range(1.0f, 2.0f).Value(1.5f).OnValueChanged([](float) {
        if (g_icon_play.syncing || !g_icon_play.weight_slider) return;
        const float size = g_icon_play.size_slider ? g_icon_play.size_slider->Value() : 16.0f;
        ApplyIconPlay(size, g_icon_play.weight_slider->Value());
    });

    g_icon_play.icons.clear();
    auto add_group = [&](const wchar_t* title, const IconSpec* specs, size_t n) {
        icons.Add<Label>(title, TextRole::Caption).Secondary(true);
        auto& grid = icons.Add<Grid>(8).Gap(8.0f, 12.0f);
        for (size_t i = 0; i < n; ++i) {
            const IconSpec& spec = specs[i];
            auto& cell = grid.Add<Column>().Spacing(4.0f).AlignCross(Cross::Center);
            auto& icon = cell.Add<IconView>(spec.glyph)
                             .Box(28.0f)
                             .IconSize(16.0f)
                             .Weight(1.5f)
                             .Background(Color{0.0f, 0.0f, 0.0f, 0.0f})
                             .Stroke(Color{0.0f, 0.0f, 0.0f, 0.0f})
                             .ToolTip(spec.name);
            g_icon_play.icons.push_back(&icon);
            cell.Add<Label>(spec.name, TextRole::Caption).Secondary(true).Alignment(Align::Center);
        }
    };
    add_group(L"Window", kWindowIcons, sizeof(kWindowIcons) / sizeof(kWindowIcons[0]));
    add_group(L"Edit", kEditIcons, sizeof(kEditIcons) / sizeof(kEditIcons[0]));
    add_group(L"Find & view", kFindIcons, sizeof(kFindIcons) / sizeof(kFindIcons[0]));
    add_group(L"Files", kFileIcons, sizeof(kFileIcons) / sizeof(kFileIcons[0]));
    add_group(L"Navigate", kNavIcons, sizeof(kNavIcons) / sizeof(kNavIcons[0]));
    add_group(L"Mail & people", kCommsIcons, sizeof(kCommsIcons) / sizeof(kCommsIcons[0]));
    add_group(L"Media", kMediaIcons, sizeof(kMediaIcons) / sizeof(kMediaIcons[0]));
    add_group(L"Status", kStatusIcons, sizeof(kStatusIcons) / sizeof(kStatusIcons[0]));
    add_group(L"System", kSystemIcons, sizeof(kSystemIcons) / sizeof(kSystemIcons[0]));
    add_group(L"Build", kDevIcons, sizeof(kDevIcons) / sizeof(kDevIcons[0]));
}

} // namespace gallery
