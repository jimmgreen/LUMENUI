#include "common.h"
#include <string>
#include <cmath>

namespace gallery {

void BuildStatus(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Status",
             L"What the system is saying. The window StatusBar stays on the root, below this pane.");

    g_job.window = &window;

    auto& job = Sample(column, L"ProgressBar / ProgressRing",
                       L"One live float. Indeterminate for 800ms, then 0..1.");
    auto& row = job.Add<Row>().Spacing(16.0f).AlignCross(Cross::Center);
    auto& ring = row.Add<ProgressRing>();
    g_job.ring = &ring;
    ring.Value(0.0f);
    auto& progress = row.Add<Column>().Spacing(6.0f);
    progress.Grow();
    auto& prog_head = progress.Add<Row>().AlignMain(Main::SpaceBetween).AlignCross(Cross::Center);
    prog_head.Add<Label>(L"Build job", TextRole::Caption).Secondary(true);
    g_job.percent = &prog_head.Add<Label>(L"0%", TextRole::CaptionStrong);
    auto& bar = progress.Add<ProgressBar>();
    g_job.bar = &bar;
    bar.Value(0.0f);
    auto& actions = job.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& run = actions.Add<Button>(L"Run", ButtonKind::Primary);
    g_job.run = &run;
    run.SizeClass(ButtonSize::Large).OnClick([] { g_job.Start(); });
    auto& abort = actions.Add<Button>(L"Abort", ButtonKind::Danger);
    g_job.abort = &abort;
    abort.Enabled(false).OnClick([] { g_job.Abort(); });
    actions.Add<Button>(L"Fail", ButtonKind::Transparent).SizeClass(ButtonSize::Small).OnClick([] {
        g_job.Fail();
    });
    actions.Add<Button>(L"Unavailable", ButtonKind::Standard)
        .Enabled(false)
        .ToolTip(L"Enabled(false)");

    auto& skel = Sample(column, L"Skeleton", L"Breathing placeholder while the job runs.");
    auto& skeleton = Wide(skel).Add<Skeleton>();
    g_job.skeleton = &skeleton;
    skeleton.Grow();
    skeleton.Lines(3).Active(false);
    auto& ready = skel.Add<SettingsCard>();
    g_job.ready = &ready;
    ready.Title(L"Build ready")
        .Description(L"Artifact is on the void.")
        .Glyph(icon::kPackage);
    ready.Add<Button>(L"Install", ButtonKind::Primary)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { ShowDialog(window); });
    ready.Visible(false);

    auto& bars = Sample(column, L"InfoBar", L"Informational / Success / Warning / Critical.");
    auto& info = bars.Add<InfoBar>(L"Idle");
    g_job.info = &info;
    info.Message(L"Press Run — Informational, then Success. Fail shows Critical. Abort shows Warning.")
        .Tone(InfoBar::InfoTone::Informational)
        .Closable(false)
        .Action(L"Retry", [] { g_job.Start(); });
    auto& update = bars.Add<InfoBar>(L"Update available");
    update.Message(L"A new artifact is on the void. Install, or dismiss the bar.")
        .Tone(InfoBar::InfoTone::Success)
        .Action(L"Install", [&window] { window.ShowToast(L"Installing artifact"); })
        .OnClosed([&window] { window.ShowToast(L"InfoBar dismissed"); });

    auto& empty = Sample(column, L"EmptyState", L"Nothing here yet.");
    auto& vacant = empty.Add<EmptyState>();
    g_job.publish_empty = &vacant;
    vacant.Title(L"尚未发布")
        .Hint(L"Publish from Collections · Carousel, or install after a build.")
        .Glyph(icon::kPackage);
    auto& ok = empty.Add<InfoBar>(L"Published");
    g_job.publish_ok = &ok;
    ok.Message(L"InfoBar · Success after Publish.")
        .Tone(InfoBar::InfoTone::Success)
        .Closable(false)
        .Visible(false);

    auto& toast = Sample(column, L"Toast", L"Motion, kind, action, sticky. Rapid clicks stack.");
    auto& toast_row = toast.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    auto& toast_motion = toast_row.Add<Segmented>();
    toast_motion.AddItem(L"淡出");
    toast_motion.AddItem(L"右滑");
    toast_motion.AddItem(L"下沉");
    toast_motion.AddItem(L"收缩");
    toast_motion.SelectedIndex(0);
    toast_motion.OnSelectionChanged([&window, &toast_motion](ptrdiff_t, ptrdiff_t) {
        static const ToastMotion kMotions[] = {ToastMotion::Fade, ToastMotion::SlideRight,
                                               ToastMotion::SlideDown, ToastMotion::Scale};
        static const wchar_t* kNames[] = {L"淡出", L"右滑", L"下沉", L"收缩"};
        const ptrdiff_t i = toast_motion.SelectedIndex();
        if (i < 0 || i >= 4) return;
        window.ToastMotion(kMotions[i]);
        window.ShowToast(std::wstring(L"Toast · ") + kNames[i]);
    });
    toast_row.Add<Button>(L"堆叠 3 条", ButtonKind::Standard)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] {
            window.ShowToast(L"Toast · 第一条");
            window.ShowToast(L"Toast · 第二条");
            window.ShowToast(L"Toast · 第三条");
        });
    auto& toast_kinds = toast.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    toast_kinds.Add<Button>(L"成功 · 撤销", ButtonKind::Subtle)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] {
            ToastData data;
            data.text = L"快照已保存";
            data.action = L"撤销";
            data.duration = 4.0f;
            data.kind = ToastKind::Success;
            data.on_action = [&window] { window.ShowToast(L"已撤销", ToastKind::Info); };
            window.ShowToast(std::move(data));
        });
    toast_kinds.Add<Button>(L"警告 · 常驻", ButtonKind::Subtle)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] {
            ToastData data;
            data.text = L"构建被中止，需要处理";
            data.duration = 0.0f;
            data.kind = ToastKind::Warning;
            window.ShowToast(std::move(data));
        });
    toast_kinds.Add<Button>(L"错误", ButtonKind::Subtle)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"无法写入 artifact", ToastKind::Error); });
    toast_kinds.Add<Button>(L"信息", ButtonKind::Subtle)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"已同步远程", ToastKind::Info); });

    auto& badge = Sample(column, L"Badge / InfoBadge", L"Capsule vs corner mark.");
    auto& badge_row = badge.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    badge_row.Add<Badge>(L"Stable", Badge::BadgeTone::Success);
    badge_row.Add<Badge>(L"v1.0", Badge::BadgeTone::Accent);
    badge_row.Add<Badge>(L"Beta", Badge::BadgeTone::Warning);
    badge_row.Add<Badge>(L"Idle", Badge::BadgeTone::Neutral);
    auto& marks = badge.Add<Row>().Spacing(16.0f).AlignCross(Cross::Center);
    marks.Add<InfoBadge>(3);
    marks.Add<InfoBadge>().Dot();
    marks.Add<InfoBadge>().Glyph(icon::kZap);
    marks.Add<IconView>(icon::kBell)
        .Box(32.0f)
        .IconSize(18.0f)
        .Badge(InfoBadgeData::Count(12));

    auto& bar_card = Sample(
        column, L"StatusBar",
        L"The live bar is the window chrome under this page. This copy shows path / count / zoom.");
    auto& editor = bar_card.Add<Column>();
    editor.Card(Panel::CardStyle::Input, 12.0f);
    editor.Clip(true);
    auto& doc = editor.Add<Column>().Padding(12.0f, 10.0f).Spacing(4.0f);
    doc.Add<Label>(L"status_bar.cpp", TextRole::CaptionStrong);
    doc.Add<Label>(L"Path grows and ellipsizes. Trailing cells stay on the right.", TextRole::Caption)
        .Secondary(true)
        .Wrap(true);
    auto& status_demo = editor.Add<StatusBar>();
    status_demo.Path(L"src\\controls\\status_bar.cpp").CountText(L"Ln 24, Col 8").Zoom(L"100%");
    status_demo.OnInvoked([&window, &status_demo](std::wstring_view id) {
        if (id == L"zoom") status_demo.Zoom(status_demo.Zoom() == L"100%" ? L"150%" : L"100%");
        window.ShowToast(status_demo.ItemText(id));
    });

    auto& spark = Sample(column, L"Sparkline / Gauge", L"Monochrome trend + 240° meter. Peak uses glow_sm.");
    auto& meters = spark.Add<Row>().Spacing(16.0f).AlignCross(Cross::Center);
    meters.Add<Sparkline>()
        .Count(32)
        .Style(SparklineStyle::Area)
        .Values([](size_t i) { return 0.35f + 0.25f * std::sin(static_cast<float>(i) * 0.4f); })
        .Grow();
    meters.Add<Gauge>().Range(0.0f, 100.0f).Value(72.0f).Threshold(90.0f).Caption(L"CPU")
        .Unit(L"%");
}

} // namespace gallery
