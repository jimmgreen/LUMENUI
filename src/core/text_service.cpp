#include "text_service.h"
#include <cmath>
#include <cwchar>
#include <iterator>
#include <string>

namespace fui {
namespace {

struct RoleSpec {
    const wchar_t* family;
    const wchar_t* fallback;
    float size;
    DWRITE_FONT_WEIGHT weight;
};

constexpr RoleSpec kRoles[] = {
    {L"Segoe UI Variable Text", L"Segoe UI", 14.0f, DWRITE_FONT_WEIGHT_NORMAL},       // Body
    {L"Segoe UI Variable Text", L"Segoe UI", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},    // BodyStrong
    {L"Segoe UI Variable Text", L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_NORMAL},       // Caption
    {L"Segoe UI Variable Text", L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},    // CaptionStrong
    {L"Segoe UI Variable Display", L"Segoe UI", 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD}, // Title
    {L"Segoe Fluent Icons", L"Segoe MDL2 Assets", 16.0f, DWRITE_FONT_WEIGHT_NORMAL},  // Icon
    {L"Cascadia Mono", L"Consolas", 13.0f, DWRITE_FONT_WEIGHT_NORMAL},                // Mono
};
static_assert(std::size(kRoles) == 7);

uint64_t HashText(std::wstring_view text) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t ch : text) {
        h ^= static_cast<uint64_t>(ch);
        h *= 1099511628211ull;
    }
    return h;
}

DWRITE_TEXT_ALIGNMENT MapAlign(Align align) {
    switch (align) {
    case Align::Center: return DWRITE_TEXT_ALIGNMENT_CENTER;
    case Align::Trailing: return DWRITE_TEXT_ALIGNMENT_TRAILING;
    default: return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

} // namespace

TextService& UiText() {
    static TextService instance;
    static const bool ready = instance.Init();
    (void)ready;
    return instance;
}

bool TextService::Init() {
    return SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                         reinterpret_cast<IUnknown**>(&factory_)));
}

const wchar_t* TextService::ResolveFamily(const wchar_t* family, const wchar_t* fallback) {
    if (!families_resolved_) {
        ComPtr<IDWriteFontCollection> collection;
        if (SUCCEEDED(factory_->GetSystemFontCollection(&collection, FALSE))) {
            struct { const wchar_t* preferred; const wchar_t* fallback; wchar_t* out; } probes[] = {
                {kRoles[0].family, kRoles[0].fallback, body_family_},
                {kRoles[4].family, kRoles[4].fallback, title_family_},
                {kRoles[5].family, kRoles[5].fallback, icon_family_},
                {kRoles[6].family, kRoles[6].fallback, mono_family_},
            };
            for (auto& probe : probes) {
                uint32_t index = 0;
                BOOL exists = FALSE;
                if (SUCCEEDED(collection->FindFamilyName(probe.preferred, &index, &exists)) && exists) {
                    wcscpy_s(probe.out, 64, probe.preferred);
                } else {
                    wcscpy_s(probe.out, 64, probe.fallback);
                }
            }
        }
        families_resolved_ = true;
    }
    if (wcscmp(family, kRoles[0].family) == 0) return body_family_;
    if (wcscmp(family, kRoles[4].family) == 0) return title_family_;
    if (wcscmp(family, kRoles[5].family) == 0) return icon_family_;
    if (wcscmp(family, kRoles[6].family) == 0) return mono_family_;
    return fallback;
}

IDWriteTextFormat* TextService::Format(TextRole role) {
    const size_t index = static_cast<size_t>(role);
    if (index >= 7) return nullptr;
    auto& format = formats_[index];
    if (!format) {
        const RoleSpec& spec = kRoles[index];
        factory_->CreateTextFormat(ResolveFamily(spec.family, spec.fallback), nullptr, spec.weight,
                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                   spec.size, L"en-US", &format);
    }
    return format.get();
}

IDWriteTextFormat* TextService::IconFormat(float size) {
    const uint32_t key = static_cast<uint32_t>(size * 2.0f + 0.5f);
    auto it = icon_formats_.find(key);
    if (it != icon_formats_.end()) return it->second.get();
    ComPtr<IDWriteTextFormat> format;
    factory_->CreateTextFormat(ResolveFamily(kRoles[5].family, kRoles[5].fallback), nullptr,
                               DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, key / 2.0f, L"en-US", &format);
    IDWriteTextFormat* result = format.get();
    icon_formats_.emplace(key, std::move(format));
    return result;
}

IDWriteTextLayout* TextService::CreateLayout(std::wstring_view text, IDWriteTextFormat* format,
                                             float width, bool wrap, Align align) {
    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = factory_->CreateTextLayout(text.data(), static_cast<uint32_t>(text.size()),
                                            format, width > 1.0f ? width : 1.0f,
                                            wrap ? 1.0e6f : 1000.0f, &layout);
    if (FAILED(hr)) return nullptr;
    layout->SetTextAlignment(MapAlign(align));
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    return layout.detach();
}

IDWriteTextLayout* TextService::LayoutForKey(IDWriteTextFormat* format, std::wstring_view text,
                                             float width, bool wrap, Align align) {
    LayoutKey key{format, static_cast<uint32_t>(width * 4.0f + 0.5f), static_cast<uint8_t>(align),
                  static_cast<uint8_t>(wrap ? 1 : 0), HashText(text)};
    auto it = layouts_.find(key);
    if (it != layouts_.end()) return it->second;
    IDWriteTextLayout* layout = CreateLayout(text, format, width, wrap, align);
    if (!layout) return nullptr;
    if (layouts_.size() >= 1024) {
        for (auto& entry : layouts_) entry.second->Release();
        layouts_.clear();
    }
    layouts_.emplace(key, layout);
    return layout;
}

IDWriteTextLayout* TextService::LineLayout(std::wstring_view text, IDWriteTextFormat* format,
                                           float max_width, Align align) {
    IDWriteTextLayout* full = LayoutForKey(format, text, max_width, false, align);
    if (!full) return nullptr;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(full->GetMetrics(&metrics))) return full;
    if (metrics.widthIncludingTrailingWhitespace <= max_width) return full;

    // 省略号截断：二分找最长可容纳前缀，结果单独入缓存。
    size_t lo = 0, hi = text.size();
    std::wstring candidate;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        candidate.assign(text.substr(0, mid)).append(L"\u2026");
        ComPtr<IDWriteTextLayout> probe;
        if (FAILED(factory_->CreateTextLayout(candidate.data(),
                                              static_cast<uint32_t>(candidate.size()), format,
                                              max_width > 1.0f ? max_width : 1.0f, 1000.0f,
                                              &probe))) {
            break;
        }
        DWRITE_TEXT_METRICS probe_metrics{};
        if (FAILED(probe->GetMetrics(&probe_metrics))) break;
        if (probe_metrics.widthIncludingTrailingWhitespace <= max_width) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    candidate.assign(text.substr(0, lo)).append(L"\u2026");
    return LayoutForKey(format, candidate, max_width, false, align);
}

IDWriteTextLayout* TextService::WrapLayout(std::wstring_view text, IDWriteTextFormat* format,
                                           float wrap_width) {
    return LayoutForKey(format, text, wrap_width, true, Align::Leading);
}

Size TextService::MeasureText(std::wstring_view text, TextRole role, float max_width) {
    IDWriteTextFormat* format = Format(role);
    if (!format) return {};
    IDWriteTextLayout* layout =
        LineLayout(text, format, max_width > 0.0f ? max_width : 1.0e5f, Align::Leading);
    if (!layout) return {};
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

float TextService::MeasureWrapped(std::wstring_view text, TextRole role, float wrap_width) {
    IDWriteTextFormat* format = Format(role);
    if (!format) return 0.0f;
    IDWriteTextLayout* layout = WrapLayout(text, format, wrap_width);
    if (!layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.height;
}

} // namespace fui
