#include "fluentui/Label.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace fui {

Size Label::Measure(Size available, const Theme&) {
    if (text_.empty()) return {0.0f, role_ == TextRole::Title ? 28.0f : 20.0f};
    Size size = UiText().MeasureText(text_, role_,
                                     available.w > 0.0f ? available.w : 1.0e5f);
    return {std::max(size.w, 1.0f), size.h};
}

void Label::Draw(Painter& painter, const Theme& theme) {
    if (text_.empty()) return;
    painter.DrawText(text_, absolute_, role_,
                     !enabled_ ? theme.text_disabled : (secondary_ ? theme.text_secondary : theme.text));
}

} // namespace fui
