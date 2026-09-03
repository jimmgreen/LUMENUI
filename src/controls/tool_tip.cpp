#include "lumen/ToolTip.h"

namespace lumen {

ToolTip::ToolTip() {
    Orientation(Orientation::Vertical);
    Spacing(6.0f);
}

Size ToolTip::Measure(Size available, const Theme& theme) {
    Size cap = available;
    if (cap.w <= 0.0f || cap.w > max_width_) cap.w = max_width_;
    return StackPanel::Measure(cap, theme);
}

} // namespace lumen
