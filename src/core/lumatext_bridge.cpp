// lumatext_bridge.cpp — LumaText 绘制实现（移植自 pulse 的成熟包装层）：
// 布局 LRU + 命令列表表面缓存（子像素相位分桶）+ 按字体族选择级联；
// 斜体/非正常拉伸/不支持的字体族/任何失败都返回 false，由调用方回退 DirectWrite。
#include "lumatext_bridge.h"
#include "text_service.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <wrl/client.h>

#if defined(LUMEN_HAS_LUMATEXT)
#include <lumatext/lumatext.hpp>
#endif

namespace lumen {
namespace {

bool EnvironmentEnabled() noexcept {
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(L"LUMEN_LUMATEXT", value, ARRAYSIZE(value));
    if (length == 0 || length >= ARRAYSIZE(value)) return true;
    return _wcsicmp(value, L"0") != 0 && _wcsicmp(value, L"off") != 0 &&
           _wcsicmp(value, L"false") != 0;
}

std::wstring FontPath(const wchar_t* file_name) {
    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    if (length == 0 || length >= ARRAYSIZE(windows)) return {};
    return (std::filesystem::path(windows) / L"Fonts" / file_name).wstring();
}

std::wstring_view FontFamily(IDWriteTextFormat* format) {
    if (!format) return {};
    static std::unordered_map<IDWriteTextFormat*, std::wstring> cache;
    if (auto it = cache.find(format); it != cache.end()) return it->second;
    const UINT32 length = format->GetFontFamilyNameLength();
    if (length == 0 || length > 256) return {};
    std::wstring result(length + 1, L'\0');
    if (FAILED(format->GetFontFamilyName(result.data(), length + 1))) return {};
    result.resize(length);
    auto inserted = cache.emplace(format, std::move(result));
    return inserted.first->second;
}

bool Contains(std::wstring_view value, const wchar_t* fragment) {
    return value.find(fragment) != std::wstring_view::npos;
}

// 正文墨迹外扩的保守估计：用于判断是否需要重建带省略号的布局。
constexpr float kInkPad = 2.0f;

} // namespace

struct LumaTextBridge::Impl {
    LumaTextStats stats;

#if defined(LUMEN_HAS_LUMATEXT)
    struct LayoutKey {
        std::uint64_t text_hash = 0;
        std::uint32_t text_len = 0;
        std::uint32_t family = 0;
        std::uint16_t weight = 400;
        std::int32_t size_64 = 0;
        std::int32_t width_64 = 0;
        DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
        lt_text_ellipsis ellipsis = LT_TEXT_ELLIPSIS_NONE;

        bool operator==(const LayoutKey&) const = default;
    };

    struct LayoutKeyHash {
        size_t operator()(const LayoutKey& key) const noexcept {
            size_t value = std::hash<std::uint64_t>{}(key.text_hash);
            const auto combine = [&value](size_t next) {
                value ^= next + 0x9e3779b9u + (value << 6) + (value >> 2);
            };
            combine(std::hash<std::uint32_t>{}(key.text_len));
            combine(std::hash<std::uint32_t>{}(key.family));
            combine(std::hash<std::uint16_t>{}(key.weight));
            combine(std::hash<std::int32_t>{}(key.size_64));
            combine(std::hash<std::int32_t>{}(key.width_64));
            combine(std::hash<int>{}(static_cast<int>(key.alignment)));
            combine(std::hash<int>{}(static_cast<int>(key.ellipsis)));
            return value;
        }
    };

    struct SurfaceKey {
        const lt_text_layout* layout = nullptr;
        // layout 指针会被 LRU 复用，文本哈希保证旧表面不会命中新字符串。
        std::size_t text_hash = 0;
        std::int32_t width_64 = 0;
        std::int32_t height_64 = 0;
        // LumaText 字形缓存有 1/8 像素相位精度，表面键按 1/8（横向）与
        // 1/4（纵向）分桶，避免平滑滚动时每帧生成新命令列表。
        std::uint8_t x_phase_8 = 0;
        std::uint8_t y_phase_8 = 0;
        std::uint32_t foreground = 0;
        std::uint32_t background = 0;
        bool dark = false;

        bool operator==(const SurfaceKey&) const = default;
    };

    struct SurfaceKeyHash {
        size_t operator()(const SurfaceKey& key) const noexcept {
            size_t value = std::hash<const void*>{}(key.layout);
            const auto combine = [&value](size_t next) {
                value ^= next + 0x9e3779b9u + (value << 6) + (value >> 2);
            };
            combine(std::hash<std::size_t>{}(key.text_hash));
            combine(std::hash<std::int32_t>{}(key.width_64));
            combine(std::hash<std::int32_t>{}(key.height_64));
            combine(std::hash<std::uint32_t>{}(
                static_cast<std::uint32_t>(key.x_phase_8) << 24 |
                static_cast<std::uint32_t>(key.y_phase_8) << 16 |
                (key.dark ? 1u : 0u)));
            combine(std::hash<std::uint32_t>{}(key.foreground));
            combine(std::hash<std::uint32_t>{}(key.background));
            return value;
        }
    };

    struct SurfaceEntry {
        Microsoft::WRL::ComPtr<ID2D1CommandList> commands;
        std::uint64_t estimated_bytes = 0;
        std::list<SurfaceKey>::iterator lru;
    };

    struct LayoutSlot {
        LumaText::TextLayout layout;
        std::list<LayoutKey>::iterator lru;
        std::wstring text;
    };

    LumaText::Context context;
    LumaText::Renderer renderer;
    LumaText::RenderProfile profile;
    LumaText::FontFace yahei_regular;
    LumaText::FontFace yahei_bold;
    LumaText::FontFace segoe_regular;
    LumaText::FontFace segoe_bold;
    LumaText::FontFace icons_face;
    LumaText::FontFace emoji_face;
    LumaText::FontCascade yahei_cascade;
    LumaText::FontCascade segoe_cascade;
    LumaText::FontCascade icons_cascade;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> target_dc;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> recording_dc;
    bool busy = false;
    std::unordered_map<LayoutKey, LayoutSlot, LayoutKeyHash> layouts;
    std::list<LayoutKey> layout_lru;
    std::unordered_map<SurfaceKey, SurfaceEntry, SurfaceKeyHash> surfaces;
    std::list<SurfaceKey> surface_lru;
    std::uint64_t surface_bytes = 0;

    static constexpr std::uint32_t kYaHei = 1;
    static constexpr std::uint32_t kSegoe = 2;
    static constexpr std::uint32_t kIcons = 3;
    // 每窗口独立缓存，保持小而热；布局被淘汰时其表面一并失效。
    static constexpr size_t kLayoutCacheLimit = 512;
    static constexpr size_t kSurfaceCountLimit = 512;
    static constexpr std::uint64_t kSurfaceCacheLimit = 8ull * 1024ull * 1024ull;
    static constexpr std::uint64_t kGlyphCacheLimit = 512ull * 1024ull;
    static constexpr std::uint64_t kMaxCachedSurfaceBytes = 64ull * 1024ull;
    static constexpr std::int32_t kUnboundedWidth64 = 10000 * 64;

    static constexpr std::uint32_t Tag(char a, char b, char c, char d) noexcept {
        return (static_cast<std::uint32_t>(a) << 24) |
               (static_cast<std::uint32_t>(b) << 16) |
               (static_cast<std::uint32_t>(c) << 8) |
               static_cast<std::uint32_t>(d);
    }

    bool MakeFace(const std::wstring& path, LumaText::FontFace& face,
                  float variable_weight = 0.0f) {
        if (path.empty() || !std::filesystem::exists(path)) return false;
        lt_font_axis axis{Tag('w', 'g', 'h', 't'), variable_weight};
        auto desc = LumaText::Descriptor<lt_font_source_desc>();
        desc.source_type = LT_FONT_SOURCE_FILE;
        desc.file_path = path.c_str();
        if (variable_weight > 0.0f) {
            desc.axes = &axis;
            desc.axis_count = 1;
        }
        return lt_font_face_create(context.get(), &desc, face.put()) == LT_OK;
    }

    bool MakeCascade(const std::vector<lt_font_cascade_entry>& entries,
                     LumaText::FontCascade& cascade) {
        auto desc = LumaText::Descriptor<lt_font_cascade_desc>();
        desc.entries = entries.data();
        desc.entry_count = static_cast<std::uint32_t>(entries.size());
        desc.allow_system_fallback = false;
        return lt_font_cascade_create(context.get(), &desc, cascade.put()) == LT_OK;
    }

    static void PushFace(std::vector<lt_font_cascade_entry>& entries,
                         const LumaText::FontFace& face, std::uint16_t weight) {
        if (face) entries.push_back({face.get(), weight, 0});
    }

    void ClearSurfaces() {
        surfaces.clear();
        surface_lru.clear();
        surface_bytes = 0;
    }

    void DropSurfacesFor(const lt_text_layout* layout) {
        if (!layout) return;
        for (auto it = surfaces.begin(); it != surfaces.end();) {
            if (it->first.layout != layout) {
                ++it;
                continue;
            }
            surface_bytes -= it->second.estimated_bytes;
            surface_lru.erase(it->second.lru);
            stats.surface_cache_evictions++;
            it = surfaces.erase(it);
        }
    }

    void TrimSurfaces() {
        while (!surfaces.empty() &&
               (surfaces.size() > kSurfaceCountLimit || surface_bytes > kSurfaceCacheLimit)) {
            const SurfaceKey oldest = surface_lru.back();
            if (const auto found = surfaces.find(oldest); found != surfaces.end()) {
                surface_bytes -= found->second.estimated_bytes;
                surfaces.erase(found);
                stats.surface_cache_evictions++;
            }
            surface_lru.pop_back();
        }
    }

    void TouchSurface(std::unordered_map<SurfaceKey, SurfaceEntry, SurfaceKeyHash>::iterator it) {
        surface_lru.splice(surface_lru.begin(), surface_lru, it->second.lru);
        it->second.lru = surface_lru.begin();
    }

    void StoreSurface(const SurfaceKey& key,
                      Microsoft::WRL::ComPtr<ID2D1CommandList> commands,
                      std::uint64_t estimated_bytes) {
        if (estimated_bytes == 0 || estimated_bytes > kMaxCachedSurfaceBytes) return;
        if (auto existing = surfaces.find(key); existing != surfaces.end()) {
            surface_bytes -= existing->second.estimated_bytes;
            surface_lru.erase(existing->second.lru);
            surfaces.erase(existing);
        }
        surface_lru.push_front(key);
        surfaces.emplace(key, SurfaceEntry{std::move(commands), estimated_bytes,
                                           surface_lru.begin()});
        surface_bytes += estimated_bytes;
        TrimSurfaces();
    }

    void EvictOldestLayout() {
        if (layout_lru.empty()) return;
        const LayoutKey oldest = layout_lru.back();
        if (const auto found = layouts.find(oldest); found != layouts.end()) {
            DropSurfacesFor(found->second.layout.get());
            layouts.erase(found);
        }
        layout_lru.pop_back();
    }

    void FillLayoutDesc(lt_text_layout_desc& desc, const LayoutKey& key, std::wstring_view text,
                        lt_text_style& style, float font_size,
                        LumaText::FontCascade& cascade) const {
        style = LumaText::Descriptor<lt_text_style>();
        style.cascade = cascade.get();
        style.font_size = font_size;
        style.weight = key.weight;
        desc = LumaText::Descriptor<lt_text_layout_desc>();
        desc.text = text.data();
        desc.text_length = static_cast<std::uint32_t>(text.size());
        desc.base_style = style;
        desc.locale = key.family == kYaHei ? "zh-CN" : "en-US";
        desc.direction = LT_TEXT_DIRECTION_AUTO;
        desc.alignment = key.alignment == DWRITE_TEXT_ALIGNMENT_CENTER
            ? LT_TEXT_ALIGNMENT_CENTER
            : key.alignment == DWRITE_TEXT_ALIGNMENT_TRAILING ? LT_TEXT_ALIGNMENT_END
                                                              : LT_TEXT_ALIGNMENT_START;
        desc.ellipsis = key.ellipsis;
        desc.max_width = key.width_64 / 64.0f;
    }

    LayoutKey MakeLayoutKey(std::wstring_view text, std::uint32_t family, IDWriteTextFormat* format,
                            std::int32_t width_64, DWRITE_TEXT_ALIGNMENT alignment,
                            lt_text_ellipsis ellipsis) const {
        LayoutKey key;
        key.text_hash = std::hash<std::wstring_view>{}(text);
        key.text_len = static_cast<std::uint32_t>(text.size());
        key.family = family;
        key.weight = static_cast<std::uint16_t>(
            std::clamp<int>(static_cast<int>(format->GetFontWeight()), 1, 1000));
        key.size_64 = 0;
        key.width_64 = width_64;
        key.alignment = alignment;
        key.ellipsis = ellipsis;
        return key;
    }

    LumaText::TextLayout* GetLayout(const LayoutKey& key, std::wstring_view text, float font_size,
                                    LumaText::FontCascade& cascade) {
        if (auto found = layouts.find(key); found != layouts.end()) {
            if (found->second.text == text) {
                layout_lru.splice(layout_lru.begin(), layout_lru, found->second.lru);
                found->second.lru = layout_lru.begin();
                return &found->second.layout;
            }
        }
        lt_text_style style{};
        lt_text_layout_desc desc{};
        FillLayoutDesc(desc, key, text, style, font_size, cascade);

        LumaText::TextLayout layout;
        if (lt_text_layout_create(context.get(), &desc, layout.put()) != LT_OK) return nullptr;
        while (layouts.size() >= kLayoutCacheLimit) EvictOldestLayout();
        layout_lru.push_front(key);
        const auto inserted = layouts.emplace(
            key, LayoutSlot{std::move(layout), layout_lru.begin(), std::wstring(text)});
        if (!inserted.second) {
            // 哈希碰撞且文本不同：不覆盖已有槽，用本次临时布局。
            layout_lru.pop_front();
            scratch_layout = std::move(layout);
            return &scratch_layout;
        }
        return &inserted.first->second.layout;
    }

    LumaText::TextLayout scratch_layout;
    std::unordered_map<std::uint64_t, float> glyph_advance;

    bool Init(IDWriteFactory* dwrite, ID2D1RenderTarget* target) {
        auto context_desc = LumaText::Descriptor<lt_context_desc>();
        context_desc.dwrite_factory = dwrite;
        context_desc.cpu_cache_limit_bytes = kGlyphCacheLimit;
        if (lt_context_create(&context_desc, context.put()) != LT_OK) return false;

        if (FAILED(target->QueryInterface(IID_PPV_ARGS(&target_dc))) || !target_dc) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID2D1Device> device;
        target_dc->GetDevice(&device);
        if (!device || FAILED(device->CreateDeviceContext(
                            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &recording_dc)) ||
            !recording_dc) {
            return false;
        }
        recording_dc->SetDpi(96.0f, 96.0f);
        recording_dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        auto renderer_desc = LumaText::Descriptor<lt_d2d_desc>();
        renderer_desc.render_target = recording_dc.Get();
        renderer_desc.manage_begin_end_draw = false;
        if (lt_d2d_renderer_create(context.get(), &renderer_desc, renderer.put()) != LT_OK) {
            return false;
        }

        if (!MakeFace(FontPath(L"msyh.ttc"), yahei_regular) ||
            !MakeFace(FontPath(L"msyhbd.ttc"), yahei_bold)) {
            return false;
        }

        // Win11 的 segoeui.ttf 是约 200KB 的桩文件，真正的字形在 Segoe UI Variable。
        const std::wstring variable = FontPath(L"SegUIVar.ttf");
        if (!MakeFace(variable, segoe_regular, 400.0f) ||
            !MakeFace(variable, segoe_bold, 700.0f)) {
            segoe_regular.reset();
            segoe_bold.reset();
            if (!MakeFace(FontPath(L"segoeui.ttf"), segoe_regular) ||
                !MakeFace(FontPath(L"segoeuib.ttf"), segoe_bold)) {
                return false;
            }
        }

        std::vector<lt_font_cascade_entry> yahei_entries;
        PushFace(yahei_entries, yahei_regular, 400);
        PushFace(yahei_entries, yahei_bold, 600);
        PushFace(yahei_entries, yahei_bold, 700);
        // Segoe UI Emoji 垫底：彩色 COLR run 由 LumaText 内部回退 DirectWrite，正文仍走 YaHei。
        MakeFace(FontPath(L"seguiemj.ttf"), emoji_face);
        PushFace(yahei_entries, emoji_face, 400);
        if (yahei_entries.empty() || !MakeCascade(yahei_entries, yahei_cascade)) return false;

        std::vector<lt_font_cascade_entry> segoe_entries;
        PushFace(segoe_entries, segoe_regular, 400);
        PushFace(segoe_entries, segoe_bold, 600);
        PushFace(segoe_entries, segoe_bold, 700);
        // Segoe 级联尾部附加 YaHei，保证中英混排不缺字。
        PushFace(segoe_entries, yahei_regular, 400);
        PushFace(segoe_entries, yahei_bold, 700);
        PushFace(segoe_entries, emoji_face, 400);
        if (segoe_entries.empty() || !MakeCascade(segoe_entries, segoe_cascade)) return false;

        // 图标字形（Segoe Fluent Icons / Segoe MDL2 Assets 同码位），使图标
        // 文本也走 LumaText 的清晰路径；加载失败时图标回退 DirectWrite。
        if (!MakeFace(FontPath(L"segfluent.ttf"), icons_face) &&
            !MakeFace(FontPath(L"SegMDL2.ttf"), icons_face) &&
            !MakeFace(FontPath(L"segmdl2.ttf"), icons_face)) {
            icons_face.reset();
        }
        if (icons_face) {
            std::vector<lt_font_cascade_entry> icons_entries;
            PushFace(icons_entries, icons_face, 400);
            MakeCascade(icons_entries, icons_cascade);
        }

        auto profile_desc = LumaText::Descriptor<lt_render_profile_desc>();
        profile_desc.light = LumaText::Descriptor<lt_render_config>();
        profile_desc.light.coverage_gamma = 0.85f;
        profile_desc.light.coverage_contrast = 1.00f;
        profile_desc.light.raster_filter = LT_RASTER_FILTER_MITCHELL;
        profile_desc.dark = profile_desc.light;
        profile_desc.regular_optical_weight = 0.06f;
        profile_desc.bold_optical_weight = 0.0f;
        return lt_render_profile_create(&profile_desc, profile.put()) == LT_OK;
    }

    void Shutdown() noexcept {
        ClearSurfaces();
        layouts.clear();
        layout_lru.clear();
        busy = false;
        icons_cascade.reset();
        segoe_cascade.reset();
        yahei_cascade.reset();
        icons_face.reset();
        emoji_face.reset();
        segoe_bold.reset();
        segoe_regular.reset();
        yahei_bold.reset();
        yahei_regular.reset();
        profile.reset();
        renderer.reset();
        recording_dc.Reset();
        target_dc.Reset();
        context.reset();
    }

    static bool ContainsCjk(std::wstring_view text) noexcept {
        for (const wchar_t c : text) {
            if ((c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
                (c >= 0xFF00 && c <= 0xFFEF)) {
                return true;
            }
        }
        return false;
    }

    static bool ContainsAscii(std::wstring_view text) noexcept {
        for (const wchar_t c : text) {
            if (c >= 0x20 && c <= 0x7E) return true;
        }
        return false;
    }

    bool ResolveCascade(IDWriteTextFormat* format, std::wstring_view text,
                        std::uint32_t& family, LumaText::FontCascade*& cascade) {
        if (!format) return false;
        const std::wstring_view family_name = FontFamily(format);
        const bool icon_font = Contains(family_name, L"Icons") ||
                               Contains(family_name, L"MDL2") ||
                               Contains(family_name, L"Assets");
        if (icon_font) {
            if (!icons_cascade.get()) return false;
            family = kIcons;
            cascade = &icons_cascade;
            return true;
        }
        const bool yahei = Contains(family_name, L"Microsoft YaHei");
        const bool segoe_text = Contains(family_name, L"Segoe UI") &&
                                !Contains(family_name, L"Emoji");
        if (!yahei && !segoe_text) return false;
        // 纯中文走 YaHei。ASCII+CJK 混排必须 Segoe 优先（级联尾部已垫 YaHei）：
        // 否则拉丁按 YaHei 宽度、绘制回退 Segoe，插入符会落到字形右侧很远。
        if (ContainsCjk(text) && yahei_cascade.get() && !ContainsAscii(text)) {
            family = kYaHei;
            cascade = &yahei_cascade;
            return true;
        }
        if (yahei) {
            family = kYaHei;
            cascade = &yahei_cascade;
            return yahei_cascade.get() != nullptr;
        }
        family = kSegoe;
        cascade = &segoe_cascade;
        return segoe_cascade.get() != nullptr;
    }

    bool DrawOn(ID2D1DeviceContext* dc, std::wstring_view text, IDWriteTextFormat* format,
                const D2D1_RECT_F& bounds, const D2D1_COLOR_F& foreground,
                const D2D1_COLOR_F& background, float scale,
                DWRITE_TEXT_ALIGNMENT alignment) {
        if (!dc || busy || !renderer || !format || text.empty() ||
            format->GetFontStyle() != DWRITE_FONT_STYLE_NORMAL ||
            format->GetFontStretch() != DWRITE_FONT_STRETCH_NORMAL ||
            text.size() > UINT32_MAX || !(scale > 0.0f)) {
            return false;
        }
        struct BusyGuard {
            bool& flag;
            explicit BusyGuard(bool& value) : flag(value) { flag = true; }
            ~BusyGuard() { flag = false; }
        } busy_guard(busy);
        const float width = bounds.right - bounds.left;
        const float height = bounds.bottom - bounds.top;
        // bounds 是物理像素，字号同样换算到物理像素，保证 1:1 栅格化不放大。
        const float font_size = format->GetFontSize() * scale;
        if (!(width > 0.0f) || !(height > 0.0f) || !(font_size > 0.0f)) return false;

        std::uint32_t family = 0;
        LumaText::FontCascade* cascade = nullptr;
        if (!ResolveCascade(format, text, family, cascade) || !cascade) return false;

        LayoutKey key = MakeLayoutKey(text, family, format, kUnboundedWidth64,
                                      DWRITE_TEXT_ALIGNMENT_LEADING, LT_TEXT_ELLIPSIS_NONE);
        key.size_64 = static_cast<std::int32_t>(std::lround(font_size * 64.0f));
        LumaText::TextLayout* layout = GetLayout(key, text, font_size, *cascade);
        if (!layout || !*layout) return false;

        auto metrics = LumaText::Descriptor<lt_text_metrics>();
        if (lt_text_layout_get_metrics(layout->get(), &metrics) != LT_OK) return false;
        // 目标矩形是墨迹裁剪：仅按步进宽度判断会把最后一个字形的 Mitchell
        // 波瓣削掉（"File" → "Fil"），因此带墨迹外扩重建省略号布局。
        if (metrics.width + kInkPad > width + 0.5f) {
            key.width_64 = static_cast<std::int32_t>(
                std::lround(std::max(1.0f, width - kInkPad) * 64.0f));
            key.ellipsis = LT_TEXT_ELLIPSIS_END;
            layout = GetLayout(key, text, font_size, *cascade);
            if (!layout || !*layout) return false;
            if (lt_text_layout_get_metrics(layout->get(), &metrics) != LT_OK) return false;
        }
        const float floor_x = std::floor(bounds.left);
        const float floor_y = std::floor(bounds.top);
        const float phase_x = bounds.left - floor_x;
        const float phase_y = bounds.top - floor_y;
        const auto phase_bucket = [](float phase, int buckets) {
            const long rounded = std::lround(phase * static_cast<float>(buckets));
            return static_cast<std::uint8_t>(
                std::clamp<long>(rounded, 0l, static_cast<long>(buckets - 1)));
        };
        const std::uint8_t x_phase_8 = phase_bucket(phase_x, 8);
        const std::uint8_t y_phase_4 = phase_bucket(phase_y, 4);
        const float cached_phase_x = static_cast<float>(x_phase_8) / 8.0f;
        const float cached_phase_y = static_cast<float>(y_phase_4) / 4.0f;
        const auto color_byte = [](float value) {
            return static_cast<std::uint32_t>(std::clamp(std::lround(value * 255.0f), 0l, 255l));
        };
        const std::uint32_t packed_foreground = color_byte(foreground.r) |
                                                (color_byte(foreground.g) << 8) |
                                                (color_byte(foreground.b) << 16) |
                                                (color_byte(foreground.a) << 24);
        const std::uint32_t packed_background = color_byte(background.r) |
                                                (color_byte(background.g) << 8) |
                                                (color_byte(background.b) << 16) |
                                                (color_byte(background.a) << 24);
        const bool dark =
            0.2126f * background.r + 0.7152f * background.g + 0.0722f * background.b < 0.5f;
        const SurfaceKey surface_key{
            layout->get(),
            std::hash<std::wstring_view>{}(text),
            static_cast<std::int32_t>(std::lround(width * 64.0f)),
            static_cast<std::int32_t>(std::lround(height * 64.0f)),
            x_phase_8,
            y_phase_4,
            packed_foreground,
            packed_background,
            dark,
        };
        if (auto cached = surfaces.find(surface_key); cached != surfaces.end()) {
            TouchSurface(cached);
            const auto offset = D2D1::Point2F(floor_x + phase_x - cached_phase_x,
                                              floor_y + phase_y - cached_phase_y);
            // 命令列表内已含文本裁剪，无需重复设置目标裁剪状态。
            dc->DrawImage(cached->second.commands.Get(), &offset, nullptr,
                          D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                          D2D1_COMPOSITE_MODE_SOURCE_OVER);
            stats.surface_cache_hits++;
            stats.draw_calls++;
            return true;
        }
        stats.surface_cache_misses++;

        Microsoft::WRL::ComPtr<ID2D1CommandList> commands;
        if (FAILED(recording_dc->CreateCommandList(&commands)) || !commands) return false;
        recording_dc->SetTarget(commands.Get());
        recording_dc->BeginDraw();
        auto frame_desc = LumaText::Descriptor<lt_frame_desc>();
        frame_desc.dpi_x = 96.0f;
        frame_desc.dpi_y = 96.0f;
        LumaText::Frame frame;
        if (lt_frame_begin(renderer.get(), &frame_desc, frame.put()) != LT_OK) {
            recording_dc->EndDraw();
            recording_dc->SetTarget(nullptr);
            return false;
        }

        auto draw = LumaText::Descriptor<lt_draw_text_desc>();
        draw.origin_x = cached_phase_x;
        if (alignment == DWRITE_TEXT_ALIGNMENT_CENTER) {
            draw.origin_x += std::max(0.0f, (width - metrics.width) * 0.5f);
        } else if (alignment == DWRITE_TEXT_ALIGNMENT_TRAILING) {
            draw.origin_x += std::max(0.0f, width - metrics.width);
        }
        draw.origin_y = cached_phase_y + (height - metrics.height) * 0.5f;
        if (family == kYaHei) {
            draw.origin_y -= lumen::UiText().CjkBaselineNudge(format->GetFontSize()) * scale;
        }
        draw.clip = {cached_phase_x, cached_phase_y, cached_phase_x + width,
                     cached_phase_y + height};
        draw.clip_enabled = true;
        draw.foreground = {foreground.r, foreground.g, foreground.b, foreground.a};
        draw.background = {background.r, background.g, background.b, background.a};
        draw.background_type = LT_BACKGROUND_TRANSPARENT;
        draw.render_config = LumaText::Descriptor<lt_render_config>();
        draw.render_config.coverage_gamma = 0.85f;
        draw.render_config.coverage_contrast = 1.00f;
        draw.render_config.raster_filter = LT_RASTER_FILTER_MITCHELL;
        draw.profile = profile.get();

        const lt_result result = lt_frame_draw_text_layout(frame.get(), layout->get(), &draw);
        auto frame_stats = LumaText::Descriptor<lt_frame_stats>();
        if (result == LT_OK && lt_frame_get_stats(frame.get(), &frame_stats) == LT_OK) {
            stats.freetype_glyphs += frame_stats.freetype_glyphs;
            stats.cache_hits += frame_stats.glyph_cache_hits;
        }
        const lt_result end_result = lt_frame_end(frame.get());
        const HRESULT recording_result = recording_dc->EndDraw();
        recording_dc->SetTarget(nullptr);
        if (result != LT_OK || end_result != LT_OK || FAILED(recording_result) ||
            FAILED(commands->Close())) {
            return false;
        }

        const float ink_width = std::min(width, std::max(1.0f, metrics.width + 2.0f));
        const std::uint64_t estimated_bytes =
            static_cast<std::uint64_t>(std::max(1l, std::lround(std::ceil(ink_width)))) *
            static_cast<std::uint64_t>(std::max(1l, std::lround(std::ceil(height)))) * 4ull;
        StoreSurface(surface_key, commands, estimated_bytes);

        const auto offset = D2D1::Point2F(floor_x + phase_x - cached_phase_x,
                                          floor_y + phase_y - cached_phase_y);
        dc->DrawImage(commands.Get(), &offset, nullptr,
                      D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                      D2D1_COMPOSITE_MODE_SOURCE_OVER);
        stats.draw_calls++;
        return true;
    }

    bool Draw(std::wstring_view text, IDWriteTextFormat* format, const D2D1_RECT_F& bounds,
              const D2D1_COLOR_F& foreground, const D2D1_COLOR_F& background, float scale,
              DWRITE_TEXT_ALIGNMENT alignment) {
        return DrawOn(target_dc.Get(), text, format, bounds, foreground, background, scale,
                      alignment);
    }

    bool Measure(std::wstring_view text, IDWriteTextFormat* format, float& width, float* height) {
        width = 0.0f;
        if (height) *height = 0.0f;
        if (!renderer || !format) return false;
        if (text.empty()) return true;
        std::uint32_t family = 0;
        LumaText::FontCascade* cascade = nullptr;
        if (!ResolveCascade(format, text, family, cascade) || !cascade) return false;
        const float font_size = format->GetFontSize();
        if (!(font_size > 0.0f)) return false;
        LayoutKey key = MakeLayoutKey(text, family, format, kUnboundedWidth64,
                                      DWRITE_TEXT_ALIGNMENT_LEADING, LT_TEXT_ELLIPSIS_NONE);
        key.size_64 = static_cast<std::int32_t>(std::lround(font_size * 64.0f));
        if (text.size() == 1) {
            const std::uint64_t gk = (static_cast<std::uint64_t>(key.size_64) << 32) ^
                                     key.text_hash ^ family;
            if (auto it = glyph_advance.find(gk); it != glyph_advance.end()) {
                width = it->second;
                if (height) *height = font_size * 1.25f;
                return true;
            }
        }
        LumaText::TextLayout* layout = GetLayout(key, text, font_size, *cascade);
        if (!layout || !*layout) return false;
        auto metrics = LumaText::Descriptor<lt_text_metrics>();
        if (lt_text_layout_get_metrics(layout->get(), &metrics) != LT_OK) return false;
        width = metrics.width;
        if (height) *height = metrics.height;
        if (text.size() == 1) {
            const std::uint64_t gk = (static_cast<std::uint64_t>(key.size_64) << 32) ^
                                     key.text_hash ^ family;
            glyph_advance.emplace(gk, width);
        }
        return true;
    }

    LumaText::TextLayout* LayoutForDraw(std::wstring_view text, IDWriteTextFormat* format,
                                        float scale) {
        if (!format || text.empty() || !(scale > 0.0f)) return nullptr;
        std::uint32_t family = 0;
        LumaText::FontCascade* cascade = nullptr;
        if (!ResolveCascade(format, text, family, cascade) || !cascade) return nullptr;
        const float font_size = format->GetFontSize() * scale;
        if (!(font_size > 0.0f)) return nullptr;
        LayoutKey key = MakeLayoutKey(text, family, format, kUnboundedWidth64,
                                      DWRITE_TEXT_ALIGNMENT_LEADING, LT_TEXT_ELLIPSIS_NONE);
        key.size_64 = static_cast<std::int32_t>(std::lround(font_size * 64.0f));
        return GetLayout(key, text, font_size, *cascade);
    }

    bool HitTestPoint(std::wstring_view text, IDWriteTextFormat* format, float scale,
                      float x_dip, size_t* caret_index) {
        if (!caret_index) return false;
        *caret_index = 0;
        if (text.empty()) return true;
        LumaText::TextLayout* layout = LayoutForDraw(text, format, scale);
        if (!layout || !*layout) return false;
        auto line = LumaText::Descriptor<lt_text_metrics>();
        if (lt_text_layout_get_metrics(layout->get(), &line) != LT_OK) return false;
        auto hit = LumaText::Descriptor<lt_hit_test_metrics>();
        const float y = std::max(line.height * 0.5f, 0.5f);
        if (lt_text_layout_hit_test_point(layout->get(), x_dip * scale, y, &hit) != LT_OK) {
            return false;
        }
        const size_t index = static_cast<size_t>(hit.text_position) +
                             (hit.is_trailing ? static_cast<size_t>(hit.text_length) : 0);
        *caret_index = std::min(index, text.size());
        return true;
    }

    bool PositionToX(std::wstring_view text, IDWriteTextFormat* format, float scale,
                     size_t caret_index, float* x_dip) {
        if (!x_dip) return false;
        *x_dip = 0.0f;
        if (text.empty()) return true;
        if (!(scale > 0.0f)) return false;
        LumaText::TextLayout* layout = LayoutForDraw(text, format, scale);
        if (!layout || !*layout) return false;
        auto metrics = LumaText::Descriptor<lt_text_metrics>();
        if (lt_text_layout_get_metrics(layout->get(), &metrics) != LT_OK) return false;
        // LayoutForDraw 与绘制同字号（物理像素），命中坐标除以 scale 才是 DIP。
        const float width_dip = metrics.width / scale;
        // pos == length 落不到任何 cluster（半开区间）；LumaText 会退到
        // clusters.back()。按视觉 left 排序后末项可能是剩余行盒，CJK 插入符会被
        // 推到无界 max_width。行尾一律用步进宽。
        if (caret_index >= text.size()) {
            *x_dip = width_dip;
            return true;
        }
        float x_px = 0.0f, y_px = 0.0f;
        auto hit = LumaText::Descriptor<lt_hit_test_metrics>();
        if (lt_text_layout_hit_test_position(layout->get(), static_cast<uint32_t>(caret_index),
                                             false, &x_px, &y_px, &hit) != LT_OK) {
            return false;
        }
        *x_dip = Clamp(x_px / scale, 0.0f, width_dip);
        return true;
    }
#else
    bool Init(IDWriteFactory*, ID2D1RenderTarget*) { return false; }
    void Shutdown() noexcept {}
    bool Draw(std::wstring_view, IDWriteTextFormat*, const D2D1_RECT_F&, const D2D1_COLOR_F&,
              const D2D1_COLOR_F&, float, DWRITE_TEXT_ALIGNMENT) {
        return false;
    }
    bool Measure(std::wstring_view, IDWriteTextFormat*, float&, float*) { return false; }
#endif
};

LumaTextBridge::LumaTextBridge() = default;
LumaTextBridge::~LumaTextBridge() = default;

bool LumaTextBridge::Init(IDWriteFactory* dwrite, ID2D1RenderTarget* target) {
    if (!EnvironmentEnabled()) return false;
    Shutdown();
    impl_ = std::make_unique<Impl>();
    if (!impl_->Init(dwrite, target)) {
        impl_.reset();
        return false;
    }
    return true;
}

void LumaTextBridge::Shutdown() noexcept {
    impl_.reset();
}

bool LumaTextBridge::Enabled() const noexcept {
    return impl_ != nullptr;
}

bool LumaTextBridge::Draw(std::wstring_view text, IDWriteTextFormat* format,
                          const D2D1_RECT_F& bounds, const D2D1_COLOR_F& foreground,
                          const D2D1_COLOR_F& background, float scale,
                          DWRITE_TEXT_ALIGNMENT alignment) {
    if (!impl_) return false;
    if (!impl_->Draw(text, format, bounds, foreground, background, scale, alignment)) {
        impl_->stats.fallback_draws++;
        return false;
    }
    return true;
}

bool LumaTextBridge::Measure(std::wstring_view text, IDWriteTextFormat* format, float& width,
                             float* height) {
    return impl_ && impl_->Measure(text, format, width, height);
}

bool LumaTextBridge::HitTestPoint(std::wstring_view text, IDWriteTextFormat* format, float scale,
                                  float x_dip, size_t* caret_index) {
#if defined(LUMEN_HAS_LUMATEXT)
    return impl_ && impl_->HitTestPoint(text, format, scale, x_dip, caret_index);
#else
    (void)text;
    (void)format;
    (void)scale;
    (void)x_dip;
    (void)caret_index;
    return false;
#endif
}

bool LumaTextBridge::PositionToX(std::wstring_view text, IDWriteTextFormat* format, float scale,
                                 size_t caret_index, float* x_dip) {
#if defined(LUMEN_HAS_LUMATEXT)
    return impl_ && impl_->PositionToX(text, format, scale, caret_index, x_dip);
#else
    (void)text;
    (void)format;
    (void)scale;
    (void)caret_index;
    (void)x_dip;
    return false;
#endif
}

const LumaTextStats& LumaTextBridge::Stats() const noexcept {
    static const LumaTextStats empty{};
    return impl_ ? impl_->stats : empty;
}

void LumaTextBridge::RecordFallback() noexcept {
    if (impl_) impl_->stats.fallback_draws++;
}

} // namespace lumen
