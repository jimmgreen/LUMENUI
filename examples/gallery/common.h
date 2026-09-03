// gallery/common.h — 分页壳、示例卡、跨页跳转。
#pragma once
#include <lumen/lumen.h>
#include <string>
#include <string_view>
#include <vector>

namespace gallery {

using Cross = lumen::StackPanel::CrossAlign;
using Main = lumen::StackPanel::MainAlign;

extern lumen::Property<float> g_glow;
extern lumen::WeakRef<lumen::Switch> g_dynamic_light;
extern lumen::WeakRef<lumen::AutoSuggestBox> g_suggest;

struct BuildJob {
    lumen::Window* window = nullptr;
    lumen::WeakRef<lumen::ProgressBar> bar;
    lumen::WeakRef<lumen::ProgressRing> ring;
    lumen::WeakRef<lumen::Label> percent;
    lumen::WeakRef<lumen::Skeleton> skeleton;
    lumen::WeakRef<lumen::SettingsCard> ready;
    lumen::WeakRef<lumen::InfoBar> info;
    lumen::WeakRef<lumen::Button> run;
    lumen::WeakRef<lumen::Button> abort;
    lumen::WeakRef<lumen::ProgressBar> wizard_bar;
    lumen::WeakRef<lumen::Label> wizard_pct;
    lumen::WeakRef<lumen::EmptyState> publish_empty;
    lumen::WeakRef<lumen::InfoBar> publish_ok;
    float value = 0.0f;
    bool complete = false;
    void Start();
    void Abort();
    void Fail();
    bool Busy() const noexcept;

private:
    enum class Phase { Idle, Wait, Run };
    Phase phase_ = Phase::Idle;
    float delay_left_ = 0.0f;
    lumen::Tween tween_{};
    lumen::Connection frame_;
    bool Tick(float dt);
};
extern BuildJob g_job;

constexpr float kWinW = 1280.0f;
constexpr float kPad = 28.0f;
constexpr float kGap = 24.0f;
constexpr float kSectionRadius = 24.0f;
constexpr float kCardRadius = 16.0f;
constexpr float kSwatchStops[5] = {0.0f, 0.35f, 0.5f, 0.75f, 1.0f};

void SetIntensity(lumen::Window& window, float value);
float LastGlow();
void BindShell(lumen::NavigationView& nav);
lumen::StackPanel& Lumen(lumen::StackPanel& panel, float radius = kCardRadius);
void PageHead(lumen::StackPanel& column, std::wstring_view title, std::wstring_view subtitle);
lumen::StackPanel& Sample(lumen::StackPanel& column, std::wstring_view title,
                          std::wstring_view hint = {});
// Sample 卡交叉轴按内容宽。铺满卡宽：Wide(card).Add<ListView>().Grow();
inline lumen::StackPanel& Wide(lumen::StackPanel& host) { return host.Add<lumen::Row>(); }
void ShowDialog(lumen::Window& window);
void ShowPage(std::wstring_view id);
void ApplyJobValue(float value);
std::vector<std::byte> MakeSceneBitmap();

void BuildOverview(lumen::StackPanel& column, lumen::Window& window);
void BuildButtons(lumen::StackPanel& column, lumen::Window& window);
void BuildInput(lumen::StackPanel& column, lumen::Window& window);
void BuildSelection(lumen::StackPanel& column, lumen::Window& window);
void BuildLayout(lumen::StackPanel& column, lumen::Window& window);
void BuildCollections(lumen::StackPanel& column, lumen::Window& window);
void BuildNavigation(lumen::StackPanel& column, lumen::Window& window);
void BuildOverlays(lumen::StackPanel& column, lumen::Window& window);
void BuildStatus(lumen::StackPanel& column, lumen::Window& window);
void BuildCharts(lumen::StackPanel& column, lumen::Window& window);

} // namespace gallery
