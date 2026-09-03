#include "common.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gallery {

lumen::Property<float> g_glow{0.5f};
lumen::WeakRef<lumen::Switch> g_dynamic_light;
lumen::WeakRef<lumen::AutoSuggestBox> g_suggest;
BuildJob g_job;

namespace {
float g_last_glow = 0.5f;
lumen::WeakRef<lumen::NavigationView> g_nav;
lumen::Window* g_glow_window = nullptr;
lumen::ScopedConnection g_glow_bind;
}

void ApplyJobValue(float value) {
    using namespace lumen;
    const float v = Clamp(value, 0.0f, 1.0f);
    g_job.value = v;
    wchar_t buf[16];
    swprintf_s(buf, L"%d%%", static_cast<int>(v * 100.0f + 0.5f));
    if (g_job.bar) g_job.bar->Value(v);
    if (g_job.ring) g_job.ring->Value(v);
    if (g_job.percent) g_job.percent->Text(buf);
    if (g_job.wizard_bar) g_job.wizard_bar->Value(v);
    if (g_job.wizard_pct) g_job.wizard_pct->Text(buf);
}

void BuildJob::Start() {
    using namespace lumen;
    if (phase_ != Phase::Idle || !window) return;
    phase_ = Phase::Wait;
    delay_left_ = 0.8f;
    complete = false;
    value = 0.0f;
    if (bar) {
        bar->Value(0.0f);
        bar->Indeterminate(true);
    }
    if (ring) {
        ring->Value(0.0f);
        ring->Indeterminate(true);
    }
    if (percent) percent->Text(L"...");
    if (wizard_pct) wizard_pct->Text(L"...");
    if (wizard_bar) {
        wizard_bar->Indeterminate(false);
        wizard_bar->Value(0.0f);
    }
    if (skeleton) {
        skeleton->Visible(true);
        skeleton->Active(true);
    }
    if (ready) ready->Visible(false);
    if (info) {
        info->Title(L"Building")
            .Message(L"ProgressBar::Indeterminate then determined 0..1 with ProgressRing.")
            .Tone(InfoBar::InfoTone::Informational);
    }
    if (run) run->Enabled(false);
    if (abort) abort->Enabled(true);
    frame_ = window->OnFrame([this](float dt) { return Tick(dt); });
}

void BuildJob::Abort() {
    using namespace lumen;
    if (phase_ == Phase::Idle) return;
    phase_ = Phase::Idle;
    frame_.Disconnect();
    tween_.Snap(value);
    if (bar) bar->Indeterminate(false);
    if (ring) ring->Indeterminate(false);
    if (wizard_bar) wizard_bar->Indeterminate(false);
    if (skeleton) skeleton->Active(false);
    if (info) {
        info->Title(L"Aborted")
            .Message(L"ButtonKind::Danger stopped the tick. Animation clock released.")
            .Tone(InfoBar::InfoTone::Warning);
    }
    if (run) run->Enabled(true);
    if (abort) abort->Enabled(false);
    if (window) window->ShowToast(L"Build aborted");
}

void BuildJob::Fail() {
    using namespace lumen;
    phase_ = Phase::Idle;
    frame_.Disconnect();
    tween_.Snap(value);
    if (bar) bar->Indeterminate(false);
    if (ring) ring->Indeterminate(false);
    if (wizard_bar) wizard_bar->Indeterminate(false);
    if (skeleton) skeleton->Active(false);
    if (info) {
        info->Title(L"Build failed")
            .Message(L"InfoBar::InfoTone::Critical — the missing tone, now shown.")
            .Tone(InfoBar::InfoTone::Critical);
    }
    if (run) run->Enabled(true);
    if (abort) abort->Enabled(false);
    if (window) window->ShowToast(L"Build failed");
}

bool BuildJob::Busy() const noexcept { return phase_ != Phase::Idle; }

bool BuildJob::Tick(float dt_seconds) {
    using namespace lumen;
    if (phase_ == Phase::Idle) return false;
    if (phase_ == Phase::Wait) {
        delay_left_ -= dt_seconds;
        if (delay_left_ > 0.0f) return true;
        phase_ = Phase::Run;
    if (bar) bar->Indeterminate(false);
        if (ring) ring->Indeterminate(false);
        if (wizard_bar) wizard_bar->Indeterminate(false);
        tween_.Play(0.0f, 1.0f, 2.4f, Ease::CssEaseOut);
        ApplyJobValue(0.0f);
        return true;
    }
    const bool live = tween_.Tick(dt_seconds);
    ApplyJobValue(tween_.Value());
    if (live) return true;
    phase_ = Phase::Idle;
    complete = true;
    ApplyJobValue(1.0f);
    if (skeleton) {
        skeleton->Active(false);
        skeleton->Visible(false);
    }
    if (ready) ready->Visible(true);
    if (info) {
        info->Title(L"Build ready")
            .Message(L"InfoBar::InfoTone::Success — determined ProgressBar and ProgressRing hit 100%.")
            .Tone(InfoBar::InfoTone::Success);
    }
    if (run) run->Enabled(true);
    if (abort) abort->Enabled(false);
    if (window) window->ShowToast(L"Build complete");
    return false;
}

float LastGlow() { return g_last_glow; }

void SetIntensity(lumen::Window& window, float value) {
    using namespace lumen;
    if (!g_glow_bind) {
        g_glow_window = &window;
        g_glow_bind = ScopedConnection(g_glow.OnChanged([](const float& v) {
            if (!g_glow_window) return;
            g_glow_window->GlowIntensity(v);
            if (v > 0.01f) g_last_glow = v;
        }));
    }
    g_glow = Clamp(value, 0.0f, 1.0f);
    window.GlowIntensity(g_glow.Get());
    if (g_glow.Get() > 0.01f) g_last_glow = g_glow.Get();
}

void BindShell(lumen::NavigationView& nav) { g_nav = &nav; }

lumen::StackPanel& Lumen(lumen::StackPanel& panel, float radius) {
    panel.Card(lumen::Panel::CardStyle::Lumen, radius);
    return panel;
}

void PageHead(lumen::StackPanel& column, std::wstring_view title, std::wstring_view subtitle) {
    using namespace lumen;
    column.Add<Label>(title, TextRole::Title).TextGlow(true);
    if (!subtitle.empty()) {
        column.Add<Label>(subtitle, TextRole::Body).Secondary(true).Wrap(true);
    }
}

lumen::StackPanel& Sample(lumen::StackPanel& column, std::wstring_view title,
                          std::wstring_view hint) {
    using namespace lumen;
    auto& wrap = column.Add<Column>().Spacing(8.0f);
    wrap.Add<Label>(title, TextRole::CaptionStrong).Secondary(true);
    if (!hint.empty()) {
        wrap.Add<Label>(hint, TextRole::Caption).Secondary(true).Wrap(true);
    }
    auto& body = wrap.Add<Column>();
    Lumen(body, kCardRadius).Padding(20.0f, 16.0f).Spacing(12.0f).AlignCross(Cross::Start);
    return body;
}

void ShowDialog(lumen::Window& window) {
    using namespace lumen;
    auto dialog = std::make_unique<Dialog>();
    dialog->Title(L"Add Component")
        .Message(L"A new luminescent primitive on the void. Spotlight, glow tokens "
                 L"and monochrome contrast stay in lockstep.\n\nName this component:")
        .CardSize(DialogSize::Standard)
        .SecondaryButton(L"Cancel")
        .PrimaryButton(L"Add")
        .DefaultButton(DialogCommand::Primary)
        .CancelButton(DialogCommand::Secondary)
        .OnResult([&window](DialogResult r) {
            if (r == DialogResult::Primary) window.ShowToast(L"Component registered");
            else if (r == DialogResult::Secondary) window.ShowToast(L"Cancelled");
        });
    dialog->Add<TextBox>().Placeholder(L"Component name");
    window.ShowDialog(std::move(dialog));
}

void ShowPage(std::wstring_view id) {
    if (g_nav) g_nav->Navigate(id);
}

std::vector<std::byte> MakeSceneBitmap() {
    constexpr int kW = 320;
    constexpr int kH = 180;
    constexpr int kHeader = 54;
    const int stride = (kW * 3 + 3) & ~3;
    std::vector<std::byte> bytes(static_cast<size_t>(kHeader + stride * kH), std::byte{0});
    const auto put16 = [&](size_t offset, uint16_t value) {
        bytes[offset] = static_cast<std::byte>(value & 0xFFu);
        bytes[offset + 1] = static_cast<std::byte>(value >> 8);
    };
    const auto put32 = [&](size_t offset, uint32_t value) {
        for (size_t i = 0; i < 4; ++i) {
            bytes[offset + i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
        }
    };
    bytes[0] = std::byte{'B'};
    bytes[1] = std::byte{'M'};
    put32(2, static_cast<uint32_t>(bytes.size()));
    put32(10, kHeader);
    put32(14, 40);
    put32(18, static_cast<uint32_t>(kW));
    put32(22, static_cast<uint32_t>(kH));
    put16(26, 1);
    put16(28, 24);
    put32(34, static_cast<uint32_t>(stride * kH));

    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(kW);
            const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(kH);
            const float aspect = static_cast<float>(kW) / static_cast<float>(kH);
            float v = 52.0f + ny * 34.0f;

            const float mx = nx - 0.70f;
            const float my = ny - 0.28f;
            const float md = std::sqrt(mx * mx * aspect * aspect + my * my);
            v += 95.0f * std::exp(-md * md / 0.045f);
            if (md < 0.09f) v = 236.0f;

            uint32_t h = static_cast<uint32_t>(x * 374761393u + y * 668265263u);
            h = (h ^ (h >> 13)) * 1274126177u;
            if (ny < 0.58f && (h & 0x1FFu) == 1u) {
                v = std::max(v, 150.0f + static_cast<float>(h % 50u));
            }

            const float hill =
                0.64f + 0.07f * std::sin(nx * 6.3f) + 0.035f * std::sin(nx * 15.0f + 1.2f);
            if (ny > hill) {
                const float gnd = 24.0f + (ny - hill) * 48.0f;
                const float rim = 78.0f * std::exp(-std::fabs(ny - hill) * 36.0f);
                v = gnd + rim;
                const float path = std::max(0.0f, 1.0f - std::fabs(nx - 0.46f) / 0.10f);
                if (ny > 0.74f) v += path * (ny - 0.74f) / 0.26f * 55.0f;
            }

            v = std::clamp(v, 0.0f, 255.0f);
            const auto g = static_cast<std::byte>(static_cast<uint8_t>(v));
            const int row = kH - 1 - y;
            const size_t pixel = static_cast<size_t>(kHeader + row * stride + x * 3);
            bytes[pixel] = g;
            bytes[pixel + 1] = g;
            bytes[pixel + 2] = g;
        }
    }
    return bytes;
}

} // namespace gallery
