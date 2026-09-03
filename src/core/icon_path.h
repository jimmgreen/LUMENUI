// icon_path.h — Phosphor Regular SVG path → ID2D1PathGeometry（绘制路径零堆，几何按 d 指针缓存）。
#pragma once
#include <d2d1.h>

namespace lumen {

struct PhosphorIcon {
    const char* d = nullptr;
    bool fill = false;  // true = FillGeometry（官方 Regular 为 16px 描边的闭合轮廓）
    bool fatten = true; // extra DrawGeometry after fill; false for solid fills (star/play)
};

const PhosphorIcon* FindPhosphorIcon(wchar_t glyph) noexcept;
HRESULT BuildSvgPath(ID2D1Factory* factory, const char* d, ID2D1PathGeometry** out);

}  // namespace lumen
