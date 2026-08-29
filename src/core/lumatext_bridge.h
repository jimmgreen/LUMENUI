// lumatext_bridge.h — LumaText 字体渲染桥接。未集成 LumaText 时编译为纯 DirectWrite 透传。
#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace fui {

struct LumaTextStats {
    std::uint64_t draw_calls = 0;
    std::uint64_t freetype_glyphs = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t surface_cache_hits = 0;
    std::uint64_t surface_cache_misses = 0;
    std::uint64_t surface_cache_evictions = 0;
    std::uint64_t fallback_draws = 0;
};

// 每窗口一份（绑定渲染线程与该窗口的 D2D 设备）。Draw/Measure 返回 false 时
// 调用方必须回退到 DirectWrite 的 DrawTextLayout 路径。
class LumaTextBridge {
public:
    LumaTextBridge();
    ~LumaTextBridge();
    LumaTextBridge(const LumaTextBridge&) = delete;
    LumaTextBridge& operator=(const LumaTextBridge&) = delete;

    bool Init(IDWriteFactory* dwrite, ID2D1RenderTarget* target);
    void Shutdown() noexcept;
    bool Enabled() const noexcept;

    // bounds 为目标表面的物理像素矩形；scale 用于把 DIP 字号换算成物理字号。
    bool Draw(std::wstring_view text, IDWriteTextFormat* format,
              const D2D1_RECT_F& bounds, const D2D1_COLOR_F& foreground,
              const D2D1_COLOR_F& background, float scale,
              DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);
    bool Measure(std::wstring_view text, IDWriteTextFormat* format,
                 float& width, float* height = nullptr);

    const LumaTextStats& Stats() const noexcept;
    void RecordFallback() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fui
