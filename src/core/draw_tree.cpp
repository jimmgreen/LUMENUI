// draw_tree.cpp — 控件子树绘制（离屏渲染与测试共用）。
#include "lumen/Painter.h"
#include "lumen/Panel.h"

namespace lumen {

void DrawControlTree(Painter& painter, const Theme& theme, Control* root) {
    DrawControlTree(painter, theme, root, {-1.0e6f, -1.0e6f, 2.0e6f, 2.0e6f});
}

void DrawControlTree(Painter& painter, const Theme& theme, Control* root, const Rect& clip) {
    if (!root || !root->Visible()) return;
    if (!root->absolute_.IsEmpty()) {
        const Rect drawn = root->spotlight_enabled_ ? root->absolute_
                                                    : root->absolute_.Inset(-kDirtyPadDip, -kDirtyPadDip);
        if (drawn.Intersect(clip).IsEmpty()) return;
    }
    root->Prepare(painter);
    root->Draw(painter, root->EffectiveTheme(theme));
    if (auto* panel = root->AsPanel()) {
        const bool clip_children = panel->ClipChildren();
        if (clip_children) painter.PushClip(panel->ChildrenClipBounds());
        panel->PushChildDraw(painter);
        const Rect child_clip = panel->MapClipToChildren(
            clip_children ? clip.Intersect(panel->ChildrenClipBounds()) : clip);
        for (size_t i = 0; i < panel->ChildCount(); ++i) {
            panel->PushChildDrawAt(i, painter);
            DrawControlTree(painter, theme, &panel->Child(i), child_clip);
            panel->PopChildDrawAt(i, painter);
        }
        panel->PopChildDraw(painter);
        if (clip_children) painter.PopClip();
        panel->DrawOverlay(painter, root->EffectiveTheme(theme));
    }
}

} // namespace lumen
