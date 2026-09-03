#include "lumen/Separator.h"
#include "lumen/Painter.h"

namespace lumen {

Size Separator::Measure(Size available, const Theme&) {
    const float w = available.w > 0.0f && available.w < 1.0e4f ? available.w : 1.0f;
    return {w, 1.0f};
}

void Separator::Draw(Painter& painter, const Theme& theme) {
    painter.FillRect(absolute_, theme.stroke_divider);
}

} // namespace lumen
