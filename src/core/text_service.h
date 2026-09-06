// text_service.h — DirectWrite 字体策略与布局缓存。设备无关，进程内单例。
#pragma once
#include "com_ptr.h"
#include "lumen/Core.h"
#include <dwrite_3.h>
#include <cstddef>
#include <list>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lumen {

class TextService {
public:
    bool Init();
    // App::Shutdown：释放全部 DWrite 对象与缓存，回到未 Init 状态（下次 Format 重新 Init）。
    void Reset();
    IDWriteFactory3* Factory() const noexcept { return factory_.get(); }

    // 角色格式；有字体族覆盖（PushFamily）时返回同角色字号/字重但换族的缓存格式。
    IDWriteTextFormat* Format(TextRole role);
    IDWriteTextFormat* IconFormat(float size);

    // 自定义字体（内存复制 / 文件引用）进进程级 IDWriteFontCollection1；返回首个族名。
    std::wstring AddFont(std::span<const std::byte> data);
    std::wstring AddFontFile(std::wstring_view path);
    // 字体族覆盖栈（FontFamilyScope）。view 必须活过作用域；空串 = 角色默认。
    void PushFamily(std::wstring_view family) noexcept;
    void PopFamily() noexcept;
    std::wstring_view CurrentFamily() const noexcept {
        return family_depth_ > 0 ? family_stack_[family_depth_ - 1] : std::wstring_view{};
    }

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
    IDWriteTextFormat* RoleFormat(TextRole role);
    IDWriteTextFormat* FamilyFormat(TextRole role, std::wstring_view family);
    void ApplyRoleFallback(IDWriteTextFormat* format, TextRole role);
    std::wstring RegisterFontFile(IDWriteFontFile* file);
    bool RebuildCustomCollection();
    bool CollectionHasFamily(IDWriteFontCollection* collection, std::wstring_view family);

    ComPtr<IDWriteFactory3> factory_;
    ComPtr<IDWriteTextFormat> formats_[kTextRoleCount];
    struct FamilyFormatEntry {
        ComPtr<IDWriteTextFormat> format;
        size_t role = 0;
    };
    std::unordered_map<uint64_t, FamilyFormatEntry> family_formats_;
    ComPtr<IDWriteInMemoryFontFileLoader> memory_loader_;
    std::vector<ComPtr<IDWriteFontFile>> custom_files_;
    ComPtr<IDWriteFontCollection1> custom_collection_;
    static constexpr size_t kFamilyDepth = 8;
    std::wstring_view family_stack_[kFamilyDepth];
    size_t family_depth_ = 0;
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
