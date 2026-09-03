// text_service.h — DirectWrite 字体策略与布局缓存。设备无关，进程内单例。
#pragma once
#include "com_ptr.h"
#include "lumen/Core.h"
#include <dwrite_3.h>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lumen {

class TextService {
public:
    bool Init();
    IDWriteFactory3* Factory() const noexcept { return factory_.get(); }

    IDWriteTextFormat* Format(TextRole role);
    IDWriteTextFormat* IconFormat(float size);

    // 单行布局（超宽自动省略号截断），按 (格式, 宽度, 对齐, 文本) 缓存。
    IDWriteTextLayout* LineLayout(std::wstring_view text, IDWriteTextFormat* format,
                                  float max_width, Align align);
    // 多行换行布局，不截断。
    IDWriteTextLayout* WrapLayout(std::wstring_view text, IDWriteTextFormat* format,
                                  float wrap_width);

    Size MeasureText(std::wstring_view text, TextRole role, float max_width = 0.0f);
    float MeasureWrapped(std::wstring_view text, TextRole role, float wrap_width);

    // 预乘透明表面用灰度 AA（ClearType 会出彩边）。gamma 1.9、对比 0.5。
    IDWriteRenderingParams* GrayscaleParams();
    // 正值表示 CJK 回退字形相对拉丁基线应上移的 DIP。
    float CjkBaselineNudge(float em_size) const noexcept;

private:
    IDWriteTextLayout* CreateLayout(std::wstring_view text, IDWriteTextFormat* format,
                                    float width, bool wrap, Align align);
    IDWriteTextLayout* LayoutForKey(IDWriteTextFormat* format, std::wstring_view text, float width,
                                    bool wrap, Align align);
    const wchar_t* ResolveFamily(const wchar_t* family, const wchar_t* fallback);

    struct LayoutKey {
        const void* format;
        uint32_t width_q;
        uint8_t align;
        uint8_t wrap;
        uint64_t text_hash;
        bool operator==(const LayoutKey& o) const noexcept {
            return format == o.format && width_q == o.width_q && align == o.align &&
                   wrap == o.wrap && text_hash == o.text_hash;
        }
    };
    struct LayoutKeyHash {
        size_t operator()(const LayoutKey& k) const noexcept {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&h](uint64_t v) {
                h ^= v; h *= 1099511628211ull;
            };
            mix(reinterpret_cast<uint64_t>(k.format));
            mix(k.width_q);
            mix(k.align);
            mix(k.wrap);
            mix(k.text_hash);
            return static_cast<size_t>(h);
        }
    };

    struct LayoutEntry {
        IDWriteTextLayout* layout = nullptr;
        std::list<LayoutKey>::iterator lru;
    };

    void TouchLayout(LayoutEntry& entry);
    void EvictOldestLayout();

    void ApplyLayoutFeatures(IDWriteTextLayout* layout, IDWriteTextFormat* format,
                             uint32_t length);
    void EnsureFontFallback();
    void CacheFontMetrics();

    ComPtr<IDWriteFactory3> factory_;
    ComPtr<IDWriteTextFormat> formats_[kTextRoleCount];
    ComPtr<IDWriteFontFallback> font_fallback_;
    ComPtr<IDWriteRenderingParams> grayscale_params_;
    ComPtr<IDWriteTypography> tabular_;
    std::unordered_map<uint32_t, ComPtr<IDWriteTextFormat>> icon_formats_;
    std::unordered_map<LayoutKey, LayoutEntry, LayoutKeyHash> layouts_;
    std::list<LayoutKey> layout_lru_;
    std::wstring ellipsis_probe_;   // 省略号二分探针，容量跨调用保留
    bool families_resolved_ = false;
    wchar_t body_family_[64] = {};
    wchar_t icon_family_[64] = {};
    wchar_t cjk_family_[64] = {};
    float cjk_nudge_em_ = 0.0f;
};

TextService& UiText();   // 进程内共享实例（DWrite 对象均设备无关）

} // namespace lumen
