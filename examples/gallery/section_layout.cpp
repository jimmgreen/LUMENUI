#include "common.h"
#include <string>

namespace gallery {
namespace {

// 画廊里演示水平分割条：ScrollViewer 主轴无限高，不能靠 Grow 分高度，
// 所以自测 Measure 固定高度，Arrange 里按拖动量摆上下栏。
class SplitStage : public lumen::Panel {
public:
    SplitStage() {
        using namespace lumen;
        top_ = &Add<Column>();
        bottom_ = &Add<Column>();
        seam_ = &Add<Splitter>(Splitter::Orientation::Horizontal);
        seam_->OnDrag([this](float delta) {
            if (absolute_.IsEmpty()) return;
            const float hit = seam_->Thickness();
            const float lo = SplitStage::kMinPane;
            const float max_h = std::max(lo, absolute_.h - lo - hit);
            top_h_ = lumen::Clamp(top_h_ + delta, lo, max_h);
            Place();
            Invalidate();
        });
    }

    lumen::StackPanel& Top() { return *top_; }
    lumen::StackPanel& Bottom() { return *bottom_; }

protected:
    bool ClipChildren() const noexcept override { return true; }

    lumen::Size Measure(lumen::Size available, const lumen::Theme& theme) override {
        const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 320.0f;
        MeasurePanes(w, kHeight, theme);
        return {w, kHeight};
    }

    void Arrange(const lumen::Rect& absolute) override {
        absolute_ = absolute;
        Place();
    }

private:
    static constexpr float kHeight = 268.0f;
    static constexpr float kMinPane = 72.0f;

    void MeasurePanes(float w, float h, const lumen::Theme& theme) {
        const float hit = seam_->Thickness();
        const float th = lumen::Clamp(top_h_, kMinPane, std::max(kMinPane, h - kMinPane - hit));
        for (size_t i = 0; i < ChildCount(); ++i) {
            if (&Child(i) == seam_) MeasureChildAt(i, {w, hit}, theme);
            else if (&Child(i) == top_) MeasureChildAt(i, {w, th}, theme);
            else MeasureChildAt(i, {w, std::max(0.0f, h - th)}, theme);
        }
    }

    void Place() {
        if (absolute_.IsEmpty() || !top_ || !bottom_ || !seam_) return;
        const float hit = seam_->Thickness();
        const float th =
            lumen::Clamp(top_h_, kMinPane, std::max(kMinPane, absolute_.h - kMinPane - hit));
        SetChildBounds(*top_, {0.0f, 0.0f, absolute_.w, th});
        SetChildBounds(*bottom_, {0.0f, th, absolute_.w, std::max(0.0f, absolute_.h - th)});
        SetChildBounds(*seam_, {0.0f, th - hit * 0.5f, absolute_.w, hit});
        for (size_t i = 0; i < ChildCount(); ++i) ArrangeChildAt(i);
    }

    lumen::StackPanel* top_ = nullptr;
    lumen::StackPanel* bottom_ = nullptr;
    lumen::Splitter* seam_ = nullptr;
    float top_h_ = 124.0f;
};

// 给 Viewbox 有限高度：ScrollViewer 里 Column 主轴无穷，Fill / UniformToFill 才看得出差别。
class ViewStage : public lumen::Panel {
public:
    ViewStage() {
        using namespace lumen;
        Card(CardStyle::Input, 12.0f);
        Clip(true);
    }

protected:
    lumen::Size Measure(lumen::Size available, const lumen::Theme& theme) override {
        const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 240.0f;
        const float inner_w = std::max(0.0f, w - 16.0f);
        const float inner_h = std::max(0.0f, kH - 16.0f);
        if (ChildCount() && ChildVisible(0)) MeasureChildAt(0, {inner_w, inner_h}, theme);
        return {w, kH};
    }

    void Arrange(const lumen::Rect& absolute) override {
        absolute_ = absolute;
        if (ChildCount() && ChildVisible(0)) {
            SetChildBounds(Child(0), {8.0f, 8.0f, std::max(0.0f, absolute.w - 16.0f),
                                       std::max(0.0f, absolute.h - 16.0f)});
            ArrangeChildAt(0);
        }
    }

    static constexpr float kH = 156.0f;
};

class ScrollStage : public lumen::Panel {
protected:
    lumen::Size Measure(lumen::Size available, const lumen::Theme& theme) override {
        const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 320.0f;
        if (ChildCount() && ChildVisible(0)) MeasureChildAt(0, {w, kH}, theme);
        return {w, kH};
    }
    void Arrange(const lumen::Rect& absolute) override {
        absolute_ = absolute;
        if (ChildCount() && ChildVisible(0)) {
            SetChildBounds(Child(0), {0.0f, 0.0f, absolute.w, absolute.h});
            ArrangeChildAt(0);
        }
    }
    static constexpr float kH = 200.0f;
};

}  // namespace

void BuildLayout(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Layout",
             L"Place things: stacks, grids, wrap, scroll, split, scale, group, expand.");

    auto tile = [](Panel& parent, std::wstring_view glyph, std::wstring_view title,
                   std::wstring_view hint) -> Column& {
        auto& card = parent.Add<Column>();
        card.Card(Panel::CardStyle::Input, 12.0f);
        card.Padding(12.0f, 12.0f).Spacing(8.0f);
        auto& head = card.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
        head.Add<IconView>(glyph)
            .Box(28.0f)
            .IconSize(16.0f)
            .Weight(1.5f)
            .CornerRadius(8.0f)
            .Background(Color{0.0f, 0.0f, 0.0f, 0.0f})
            .Stroke(Color{0.0f, 0.0f, 0.0f, 0.0f});
        head.Add<Label>(title, TextRole::BodyStrong);
        card.Add<Label>(hint, TextRole::Caption).Secondary(true).Wrap(true);
        return card;
    };

    auto& stacks = Sample(column, L"Row / Column / Grid / Wrap",
                          L"Grow basis is 0. Grid(0, 1, 0) is auto / 1fr / auto.");
    auto& chrome = stacks.Add<Grid>(0.0, 1.0, 0.0).Gap(8.0f);
    auto& brand = chrome.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    brand.Add<IconView>(icon::kPackage)
        .Box(22.0f)
        .IconSize(16.0f)
        .Weight(1.5f)
        .CornerRadius(4.0f)
        .Background(Color{0.0f, 0.0f, 0.0f, 0.0f})
        .Stroke(Color{0.0f, 0.0f, 0.0f, 0.0f});
    brand.Add<Label>(L"LUMEN", TextRole::BodyStrong);
    chrome.Add<Label>(L"1fr fills the middle", TextRole::Caption)
        .Secondary(true)
        .Alignment(Align::Center);
    auto& caps = chrome.Add<Row>().Spacing(4.0f).AlignCross(Cross::Center).AlignMain(Main::End);
    caps.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kMinimize)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Minimize"); });
    caps.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kMaximize)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Maximize"); });
    caps.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kClose)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Close"); });

    auto& spaced = stacks.Add<Row>().AlignCross(Cross::Center);
    spaced.Add<Label>(L"Leading cluster", TextRole::Body);
    spaced.Add<Spacer>();
    auto& trailing = spaced.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    trailing.Add<Button>(L"Cancel", ButtonKind::Subtle).SizeClass(ButtonSize::Small);
    trailing.Add<Button>(L"Save", ButtonKind::Primary)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Saved"); });

    auto& grow = stacks.Add<Row>().Spacing(8.0f);
    tile(grow, icon::kLayers, L"Grow 1", L"flex 1fr").Grow();
    tile(grow, icon::kPackage, L"Grow 1", L"flex 1fr").Grow();
    tile(grow, icon::kSparkle, L"Grow 2", L"flex 2fr").Grow(2.0f);

    auto& equal = stacks.Add<Grid>(3).Gap(8.0f);
    tile(equal, icon::kHome, L"Start", L"Cross Stretch");
    tile(equal, icon::kSettings, L"Center", L"Gap 8");
    tile(equal, icon::kFolder, L"End", L"Equal columns");

    auto& form = stacks.Add<Column>();
    form.Card(Panel::CardStyle::Input, 12.0f);
    form.Padding(16.0f, 14.0f).Spacing(10.0f);
    form.Add<Label>(L"Nested Column", TextRole::BodyStrong);
    form.Add<TextBox>().Placeholder(L"Display name").Glyph(icon::kContact);
    auto& pair = form.Add<Row>().Spacing(8.0f);
    pair.Add<TextBox>().Placeholder(L"City").Grow();
    pair.Add<NumberBox>().Range(1.0, 99.0).Value(16.0).Grow();
    form.Add<Spacer>(4.0f);
    auto& foot = form.Add<Row>().AlignMain(Main::End).Spacing(8.0f);
    foot.Add<Button>(L"Reset", ButtonKind::Subtle).SizeClass(ButtonSize::Small);
    foot.Add<Button>(L"Apply", ButtonKind::Standard)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Layout applied"); });

    auto& wrap = stacks.Add<WrapPanel>().Gap(8.0f, 8.0f);
    wrap.Card(Panel::CardStyle::Input, 12.0f);
    wrap.Padding(12.0f, 12.0f);
    const wchar_t* chips[] = {L"Row", L"Column", L"Grid", L"Grow", L"WrapPanel",
                              L"SplitView", L"ScrollViewer", L"Spacer"};
    for (const wchar_t* name : chips) wrap.Add<Chip>(name);

    auto& scroll = Sample(column, L"ScrollViewer",
                          L"Wheel coasts on a spring; overscroll rubber-bands. Jump uses ScrollIntoView. Scroll down, then Prepend — the line on screen stays put.");
    auto& tools = scroll.Add<Row>().Spacing(8.0f);
    auto& inner = scroll.Add<ScrollStage>().Add<ScrollViewer>();
    auto& inner_col = inner.Add<Column>().Padding(12.0f, 10.0f).Spacing(8.0f);
    auto& banner = inner_col.Add<Label>(L"Inserted header", TextRole::BodyStrong);
    banner.Visible(false);
    Label* line16 = nullptr;
    for (int i = 1; i <= 20; ++i) {
        auto& line = inner_col.Add<Label>(L"Line " + std::to_wstring(i), TextRole::Caption)
                         .Secondary(true);
        if (i == 16) line16 = &line;
    }
    tools.Add<Button>(L"Jump to 16")
        .SizeClass(ButtonSize::Small)
        .OnClick([&inner, line16] {
            if (line16) inner.ScrollIntoView(*line16, ScrollAlignment::Start);
        });
    tools.Add<Button>(L"Prepend")
        .SizeClass(ButtonSize::Small)
        .OnClick([&banner] { banner.Visible(true); });
    tools.Add<Button>(L"Reset")
        .SizeClass(ButtonSize::Small)
        .OnClick([&inner, &banner] {
            banner.Visible(false);
            inner.ScrollToY(0.0f);
        });

    auto& splitter = Sample(column, L"Splitter", L"Drag the horizontal seam. Glow breathes on hover.");
    auto& stage = splitter.Add<SplitStage>();
    stage.Card(Panel::CardStyle::Input, 12.0f);
    stage.Top().Padding(12.0f, 10.0f).Spacing(6.0f);
    auto& source_head = stage.Top().Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    source_head.Add<IconView>(icon::kCode)
        .Box(22.0f)
        .IconSize(14.0f)
        .Weight(1.5f)
        .CornerRadius(6.0f)
        .Background(Color{0.0f, 0.0f, 0.0f, 0.0f})
        .Stroke(Color{0.0f, 0.0f, 0.0f, 0.0f});
    source_head.Add<Label>(L"scene.lumen", TextRole::CaptionStrong);
    stage.Top().Add<Label>(L"Splitter Horizontal", TextRole::Mono).Secondary(true);
    stage.Top().Add<Label>(L"hover → glow breathe", TextRole::Mono).Secondary(true);
    stage.Top().Add<Label>(L"drag  → bloom", TextRole::Mono).Secondary(true);
    stage.Bottom().Padding(12.0f, 10.0f).Spacing(8.0f);
    auto& preview_head = stage.Bottom().Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    preview_head.Add<Label>(L"Preview", TextRole::CaptionStrong);
    preview_head.Add<Badge>(L"live", Badge::BadgeTone::Accent);
    auto& preview_row = stage.Bottom().Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    preview_row.Add<ProgressRing>().Value(0.62f).Box(36.0f);
    preview_row.Add<Label>(L"Hover the seam, then drag.", TextRole::Caption)
        .Secondary(true)
        .Wrap(true)
        .Grow();

    auto& split = Sample(column, L"SplitView",
                         L"Navigation drawer, not a splitter. CompactLength(48) leaves glyphs.");
    auto& splitview = Wide(split).Add<SplitView>();
    splitview.Grow();
    splitview.PaneLength(180.0f)
        .CompactLength(48.0f)
        .Mode(SplitView::PaneMode::Compact)
        .Card(Panel::CardStyle::Input, 12.0f);
    splitview.OnToggled([&window](bool collapsed) {
        window.ShowToast(collapsed ? L"SplitView collapsed" : L"SplitView expanded");
    });
    splitview.Pane().Padding(8.0f, 10.0f).Spacing(4.0f);
    Button* pane_btns[4] = {};
    const wchar_t* pane_labels[4] = {L"总览", L"构建", L"发布", L"设置"};
    const wchar_t* pane_glyphs[4] = {icon::kLayers, icon::kGrid, icon::kPackage, icon::kSettings};
    for (int i = 0; i < 4; ++i) {
        auto& btn = splitview.Pane().Add<Button>(pane_labels[i], ButtonKind::Transparent);
        btn.Glyph(pane_glyphs[i]).SizeClass(ButtonSize::Small).ToolTip(pane_labels[i]);
        pane_btns[i] = &btn;
    }
    splitview.Content().Padding(16.0f, 12.0f).Spacing(10.0f);
    auto& page_overview = splitview.Content().Add<Column>().Spacing(8.0f);
    page_overview.Add<Label>(L"总览", TextRole::BodyStrong);
    page_overview.Add<Label>(L"折叠后侧栏只留字形。", TextRole::Caption).Secondary(true).Wrap(true);
    page_overview.Add<Row>().Add<Button>(L"折叠 / 展开", ButtonKind::Standard)
        .SizeClass(ButtonSize::Small)
        .OnClick([&splitview] { splitview.Collapse(!splitview.Collapsed()); });
    auto& page_build = splitview.Content().Add<Column>().Spacing(8.0f);
    page_build.Add<Label>(L"构建", TextRole::BodyStrong);
    page_build.Add<Row>().Add<HyperlinkButton>(L"Jump to Status").OnClick([] {
        ShowPage(L"status");
    });
    page_build.Visible(false);
    auto& page_publish = splitview.Content().Add<Column>().Spacing(8.0f);
    page_publish.Add<EmptyState>()
        .Title(L"还没有项目")
        .Hint(L"EmptyState lives on Status. This pane only hosts it.")
        .Glyph(icon::kPackage)
        .Action(L"去 Status", [] { ShowPage(L"status"); });
    page_publish.Visible(false);
    auto& page_settings = splitview.Content().Add<Column>().Spacing(8.0f);
    page_settings.Add<Label>(L"设置", TextRole::BodyStrong);
    page_settings.Add<Row>().Add<HyperlinkButton>(L"Jump to Overview").OnClick([] {
        ShowPage(L"overview");
    });
    page_settings.Visible(false);
    StackPanel* pages[4] = {&page_overview, &page_build, &page_publish, &page_settings};
    auto select_pane = [pane_btns, pages](int index) {
        for (int i = 0; i < 4; ++i) {
            if (pane_btns[i]) {
                pane_btns[i]->Kind(i == index ? ButtonKind::Subtle : ButtonKind::Transparent);
            }
            if (pages[i]) pages[i]->Visible(i == index);
        }
    };
    select_pane(0);
    for (int i = 0; i < 4; ++i) {
        pane_btns[i]->OnClick([select_pane, i] { select_pane(i); });
    }

    auto& viewbox = Sample(column, L"Viewbox", L"Children layout at natural size, then scale.");
    auto add_cluster = [](Viewbox& vb) {
        auto& cluster = vb.Add<Column>().Spacing(8.0f).AlignCross(Cross::Start);
        cluster.Add<Button>(L"Primary", ButtonKind::Primary);
        cluster.Add<Button>(L"Standard");
        auto& pair = cluster.Add<Row>().Spacing(8.0f);
        pair.Add<Button>(L"One").SizeClass(ButtonSize::Small);
        pair.Add<Button>(L"Two").SizeClass(ButtonSize::Small);
    };
    auto& modes = viewbox.Add<Grid>(2).Gap(12.0f);
    {
        auto& col = modes.Add<Column>().Spacing(6.0f);
        col.Add<Label>(L"Uniform", TextRole::Caption).Secondary(true);
        auto& uniform_pane = col.Add<ViewStage>();
        add_cluster(uniform_pane.Add<Viewbox>().Stretch(ViewboxStretch::Uniform));
    }
    {
        auto& col = modes.Add<Column>().Spacing(6.0f);
        col.Add<Label>(L"Fill", TextRole::Caption).Secondary(true);
        auto& fill_pane = col.Add<ViewStage>();
        add_cluster(fill_pane.Add<Viewbox>().Stretch(ViewboxStretch::Fill));
    }
    auto& crop = viewbox.Add<Column>().Spacing(6.0f);
    crop.Add<Label>(L"UniformToFill · crop overflow", TextRole::Caption).Secondary(true);
    auto& crop_stage = crop.Add<ViewStage>();
    add_cluster(crop_stage.Add<Viewbox>().Stretch(ViewboxStretch::UniformToFill));

    auto& group = Sample(column, L"GroupBox", L"Light grouping. Title breaks the top edge.");
    auto& net = group.Add<GroupBox>(L"Network");
    net.Add<TextBox>().Placeholder(L"Host · lumen.local");
    auto& tls = net.Add<Row>().AlignMain(Main::SpaceBetween).AlignCross(Cross::Center);
    tls.Add<Label>(L"Require TLS", TextRole::Caption).Secondary(true);
    tls.Add<Switch>();
    auto& keys = group.Add<GroupBox>(L"Shortcuts");
    keys.Add<HotkeyBox>().Chord(L"Ctrl+K");

    auto& expand = Sample(column, L"Expander / SettingsCard",
                          L"Neither turns on spotlight by itself. Sample cards do.");
    auto& expander = expand.Add<Expander>(L"Glow tokens and Spotlight");
    expander.Add<Label>(L"glow_sm / md / lg scale with glow_intensity. Spotlight is Lumen only.",
                        TextRole::Caption)
        .Secondary(true)
        .Wrap(true);
    expander.Expanded(true);
    auto& light = expand.Add<SettingsCard>();
    light.Title(L"Format on save")
        .Description(L"Apply clang-format on commit.")
        .Glyph(icon::kCode);
    light.Add<Switch>().Checked(true);

    auto& cards = Sample(column, L"CardStyle", L"Lumen / Input / Subtle. Spotlight only on Lumen.");
    auto& trio = cards.Add<Grid>(3).Gap(12.0f);
    auto& card_lumen = trio.Add<Column>();
    card_lumen.Card(Panel::CardStyle::Lumen, kCardRadius);
    card_lumen.Padding(16.0f, 14.0f).Spacing(6.0f);
    card_lumen.Spotlight(true);
    card_lumen.Add<Label>(L"Lumen", TextRole::BodyStrong);
    card_lumen.Add<Label>(L"Spotlight(true)", TextRole::Caption).Secondary(true).Wrap(true);
    auto& card_input = trio.Add<Column>();
    card_input.Card(Panel::CardStyle::Input, kCardRadius);
    card_input.Padding(16.0f, 14.0f).Spacing(6.0f);
    card_input.Add<Label>(L"Input", TextRole::BodyStrong);
    card_input.Add<Label>(L"fill_input", TextRole::Caption).Secondary(true).Wrap(true);
    auto& card_subtle = trio.Add<Column>();
    card_subtle.Card(Panel::CardStyle::Subtle, kCardRadius);
    card_subtle.Padding(16.0f, 14.0f).Spacing(6.0f);
    card_subtle.Add<Label>(L"Subtle", TextRole::BodyStrong);
    card_subtle.Add<Label>(L"quiet surface", TextRole::Caption).Secondary(true).Wrap(true);
}

}  // namespace gallery
