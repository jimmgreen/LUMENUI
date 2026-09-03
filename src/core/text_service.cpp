#include "text_service.h"
#include "log.h"
#include <cmath>
#include <cwchar>
#include <iterator>
#include <list>
#include <string>

namespace lumen {
namespace {

struct RoleSpec {
    const wchar_t* family;
    const wchar_t* fallback;
    float size;
    DWRITE_FONT_WEIGHT weight;
    bool tabular;
    float tracking_em;
};

constexpr RoleSpec kRoles[] = {
    {L"Segoe UI Variable Text", L"Segoe UI", 14.0f, DWRITE_FONT_WEIGHT_NORMAL, false, 0.0f},
    {L"Segoe UI Variable Text", L"Segoe UI", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, 0.0f},
    {L"Segoe UI Variable Text", L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, true, 0.06f},
    {L"Segoe UI Variable Text", L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, 0.06f},
    {L"Segoe UI Variable Text", L"Segoe UI", 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, 0.0f},
    {L"Segoe Fluent Icons", L"Segoe MDL2 Assets", 16.0f, DWRITE_FONT_WEIGHT_NORMAL, false, 0.0f},
    {L"Segoe UI Variable Text", L"Segoe UI", 12.0f, DWRITE_FONT_WEIGHT_NORMAL, true, 0.0f},
    {L"Segoe UI Variable Text", L"Segoe UI", 48.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, 0.0f},
    {L"Segoe UI Variable Text", L"Segoe UI", 16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, 0.0f},
    {L"Segoe UI Variable Text", L"Segoe UI", 11.0f, DWRITE_FONT_WEIGHT_NORMAL, false, 0.06f},
    {L"Segoe UI Variable Text", L"Segoe UI", 14.0f, DWRITE_FONT_WEIGHT_NORMAL, true, 0.0f},
};
static_assert(std::size(kRoles) == kTextRoleCount);

DWRITE_UNICODE_RANGE kCjkRanges[] = {
    {0x2E80, 0x2EFF}, {0x2F00, 0x2FDF}, {0x3000, 0x303F}, {0x3040, 0x30FF},
    {0x3100, 0x312F}, {0x31A0, 0x31BF}, {0x31F0, 0x31FF}, {0x3200, 0x32FF},
    {0x3300, 0x33FF}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF},
    {0xFE30, 0xFE4F}, {0xFF00, 0xFFEF},
};

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
            struct {
                const wchar_t* preferred;
                const wchar_t* fallback;
                wchar_t* out;
            } probes[] = {
                {kRoles[0].family, kRoles[0].fallback, body_family_},
                {kRoles[5].family, kRoles[5].fallback, icon_family_},
                {L"Microsoft YaHei UI", L"Microsoft YaHei", cjk_family_},
            };
            for (auto& probe : probes) {
                uint32_t index = 0;
                BOOL exists = FALSE;
                if (SUCCEEDED(collection->FindFamilyName(probe.preferred, &index, &exists)) &&
                    exists) {
                    wcscpy_s(probe.out, 64, probe.preferred);
                } else {
                    BOOL fallback_exists = FALSE;
                    if (SUCCEEDED(collection->FindFamilyName(probe.fallback, &index,
                                                             &fallback_exists)) &&
                        fallback_exists) {
                        wcscpy_s(probe.out, 64, probe.fallback);
                    } else {
                        wcscpy_s(probe.out, 64, probe.fallback);
                    }
                    static bool logged = false;
                    if (!logged) {
                        logged = true;
                        Log(LogLevel::Info, L"font fallback: %s -> %s", probe.preferred, probe.out);
                    }
                }
            }
        }
        families_resolved_ = true;
        EnsureFontFallback();
        CacheFontMetrics();
    }
    if (wcscmp(family, kRoles[0].family) == 0) return body_family_;
    if (wcscmp(family, kRoles[5].family) == 0) return icon_family_;
    return fallback;
}

void TextService::EnsureFontFallback() {
    if (font_fallback_ || !factory_ || cjk_family_[0] == 0) return;
    ComPtr<IDWriteFactory2> factory2;
    if (FAILED(factory_->QueryInterface(__uuidof(IDWriteFactory2),
                                        reinterpret_cast<void**>(&factory2))) ||
        !factory2) {
        return;
    }
    ComPtr<IDWriteFontFallback> system_fallback;
    factory2->GetSystemFontFallback(&system_fallback);
    ComPtr<IDWriteFontFallbackBuilder> builder;
    if (FAILED(factory2->CreateFontFallbackBuilder(&builder)) || !builder) return;
    const wchar_t* names[] = {cjk_family_};
    if (FAILED(builder->AddMapping(kCjkRanges, static_cast<UINT32>(std::size(kCjkRanges)), names, 1,
                                   nullptr, nullptr, nullptr, 1.0f))) {
        return;
    }
    if (system_fallback) builder->AddMappings(system_fallback.get());
    builder->CreateFontFallback(&font_fallback_);
}

void TextService::CacheFontMetrics() {
    if (!factory_) return;
    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory_->GetSystemFontCollection(&collection, FALSE)) || !collection) return;
    auto face_of = [&](const wchar_t* family_name) -> ComPtr<IDWriteFontFace> {
        if (!family_name || family_name[0] == 0) return {};
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (FAILED(collection->FindFamilyName(family_name, &index, &exists)) || !exists) return {};
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(index, &family)) || !family) return {};
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                DWRITE_FONT_STYLE_NORMAL, &font)) ||
            !font) {
            return {};
        }
        ComPtr<IDWriteFontFace> face;
        font->CreateFontFace(&face);
        return face;
    };
    ComPtr<IDWriteFontFace> latin = face_of(body_family_[0] ? body_family_ : L"Segoe UI");
    ComPtr<IDWriteFontFace> cjk = face_of(cjk_family_);
    if (!latin || !cjk) return;
    DWRITE_FONT_METRICS latin_m{};
    DWRITE_FONT_METRICS cjk_m{};
    latin->GetMetrics(&latin_m);
    cjk->GetMetrics(&cjk_m);
    if (latin_m.designUnitsPerEm == 0 || cjk_m.designUnitsPerEm == 0) return;
    const float latin_em =
        static_cast<float>(latin_m.ascent) / static_cast<float>(latin_m.designUnitsPerEm);
    const float cjk_em =
        static_cast<float>(cjk_m.ascent) / static_cast<float>(cjk_m.designUnitsPerEm);
    cjk_nudge_em_ = Clamp(latin_em - cjk_em, -0.20f, 0.20f);
}

IDWriteTextFormat* TextService::Format(TextRole role) {
    const size_t index = static_cast<size_t>(role);
    if (index >= kTextRoleCount) return nullptr;
    auto& format = formats_[index];
    if (!format) {
        const RoleSpec& spec = kRoles[index];
        factory_->CreateTextFormat(ResolveFamily(spec.family, spec.fallback), nullptr, spec.weight,
                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                   spec.size, L"en-US", &format);
        if (format && font_fallback_ && role != TextRole::Icon) {
            ComPtr<IDWriteTextFormat1> format1;
            if (SUCCEEDED(format->QueryInterface(__uuidof(IDWriteTextFormat1),
                                                 reinterpret_cast<void**>(&format1))) &&
                format1) {
                format1->SetFontFallback(font_fallback_.get());
            }
        }
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
    ApplyLayoutFeatures(layout.get(), format, static_cast<uint32_t>(text.size()));
    return layout.detach();
}

void TextService::TouchLayout(LayoutEntry& entry) {
    layout_lru_.splice(layout_lru_.begin(), layout_lru_, entry.lru);
    entry.lru = layout_lru_.begin();
}

void TextService::EvictOldestLayout() {
    if (layout_lru_.empty()) return;
    const LayoutKey oldest = layout_lru_.back();
    if (auto found = layouts_.find(oldest); found != layouts_.end()) {
        if (found->second.layout) found->second.layout->Release();
        layouts_.erase(found);
    }
    layout_lru_.pop_back();
}

IDWriteTextLayout* TextService::LayoutForKey(IDWriteTextFormat* format, std::wstring_view text,
                                             float width, bool wrap, Align align) {
    LayoutKey key{format, static_cast<uint32_t>(width * 4.0f + 0.5f), static_cast<uint8_t>(align),
                  static_cast<uint8_t>(wrap ? 1 : 0), HashText(text)};
    auto it = layouts_.find(key);
    if (it != layouts_.end()) {
        TouchLayout(it->second);
        return it->second.layout;
    }
    IDWriteTextLayout* layout = CreateLayout(text, format, width, wrap, align);
    if (!layout) return nullptr;
    if (layouts_.size() >= 1024) EvictOldestLayout();
    layout_lru_.push_front(key);
    layouts_.emplace(key, LayoutEntry{layout, layout_lru_.begin()});
    return layout;
}

IDWriteTextLayout* TextService::LineLayout(std::wstring_view text, IDWriteTextFormat* format,
                                           float max_width, Align align) {
    IDWriteTextLayout* full = LayoutForKey(format, text, max_width, false, align);
    if (!full) return nullptr;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(full->GetMetrics(&metrics))) return full;
    if (metrics.widthIncludingTrailingWhitespace <= max_width) return full;

    // wrap=2：按原文 + 宽度 + 字体缓存截断结果，溢出绘制命中后不再二分。
    LayoutKey ellipsis_key{format, static_cast<uint32_t>(max_width * 4.0f + 0.5f),
                           static_cast<uint8_t>(align), 2, HashText(text)};
    auto cached = layouts_.find(ellipsis_key);
    if (cached != layouts_.end()) {
        TouchLayout(cached->second);
        return cached->second.layout;
    }

    size_t lo = 0, hi = text.size();
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        ellipsis_probe_.assign(text.data(), mid);
        ellipsis_probe_.push_back(L'\u2026');
        ComPtr<IDWriteTextLayout> probe;
        if (FAILED(factory_->CreateTextLayout(ellipsis_probe_.data(),
                                              static_cast<uint32_t>(ellipsis_probe_.size()), format,
                                              max_width > 1.0f ? max_width : 1.0f, 1000.0f,
                                              &probe))) {
            break;
        }
        ApplyLayoutFeatures(probe.get(), format, static_cast<uint32_t>(ellipsis_probe_.size()));
        DWRITE_TEXT_METRICS probe_metrics{};
        if (FAILED(probe->GetMetrics(&probe_metrics))) break;
        if (probe_metrics.widthIncludingTrailingWhitespace <= max_width) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    ellipsis_probe_.assign(text.data(), lo);
    ellipsis_probe_.push_back(L'\u2026');
    IDWriteTextLayout* truncated = CreateLayout(ellipsis_probe_, format, max_width, false, align);
    if (!truncated) return full;
    if (layouts_.size() >= 1024) EvictOldestLayout();
    layout_lru_.push_front(ellipsis_key);
    layouts_.emplace(ellipsis_key, LayoutEntry{truncated, layout_lru_.begin()});
    return truncated;
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

void TextService::ApplyLayoutFeatures(IDWriteTextLayout* layout, IDWriteTextFormat* format,
                                      uint32_t length) {
    if (!layout || !format || length == 0) return;
    const RoleSpec* spec = nullptr;
    for (size_t i = 0; i < kTextRoleCount; ++i) {
        if (formats_[i].get() == format) {
            spec = &kRoles[i];
            break;
        }
    }
    if (!spec) return;
    const DWRITE_TEXT_RANGE range{0, length};
    if (spec->tabular) {
        if (!tabular_ && factory_) {
            factory_->CreateTypography(&tabular_);
            if (tabular_) {
                const DWRITE_FONT_FEATURE feature{DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1};
                tabular_->AddFontFeature(feature);
            }
        }
        if (tabular_) layout->SetTypography(tabular_.get(), range);
    }
    if (spec->tracking_em > 0.0f) {
        ComPtr<IDWriteTextLayout1> layout1;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                                             reinterpret_cast<void**>(&layout1))) &&
            layout1) {
            const float extra = spec->tracking_em * format->GetFontSize();
            layout1->SetCharacterSpacing(0.0f, extra, 0.0f, range);
        }
    }
}

IDWriteRenderingParams* TextService::GrayscaleParams() {
    if (!grayscale_params_ && factory_) {
        factory_->CreateCustomRenderingParams(1.9f, 0.5f, 1.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                                              DWRITE_RENDERING_MODE_NATURAL, &grayscale_params_);
    }
    return grayscale_params_.get();
}

float TextService::CjkBaselineNudge(float em_size) const noexcept {
    if (!(em_size > 0.0f) || cjk_nudge_em_ == 0.0f) return 0.0f;
    return cjk_nudge_em_ * em_size;
}

} // namespace lumen
