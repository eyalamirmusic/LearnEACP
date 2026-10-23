#pragma once

#include "Look.h"

#include <ECodeWidgets/WidgetHost.h>

#include <eacp/GPU/GPU.h>
#include <eacp/Sprites/Sprites.h>

#include <optional>

namespace Tour
{
// A piece of the Tour's chrome: a tree of ECode widgets drawn in a GPUView.
//
// It owns the GPU resources the widgets draw with. The glyph atlas cannot be
// built before the view is on a display, has to be rebuilt when it moves to one
// with a different scale, and every glyph a frame needs must be rasterized and
// uploaded before that frame's first draw. The widgets themselves are plain
// logic over rectangles. (After shadertoyeacp's Gallery, which does the same.)
//
// The chrome never takes the keyboard: the keys belong to the chapter.
class WidgetView : public eacp::GPU::GPUView
{
public:
    WidgetView();

protected:
    // Attaches the tree, points its repaints at this view and sizes it. A
    // subclass whose root is one of its own members calls this from its
    // constructor body, once the member exists.
    void setRoot(ecode::Widget& root);

private:
    void ensureRenderer();

    // Sizes the sprite renderer, the glyph batch and the tree to the view as it
    // stands. Called from render() too, because a view rendered straight after
    // setBounds has not necessarily had its resized() yet.
    void ensureSurface();

    void resized() override;
    void backingScaleChanged() override;
    void render(eacp::GPU::Frame& frame) override;

    void mouseDown(const eacp::Graphics::MouseEvent& event) override;
    void mouseDragged(const eacp::Graphics::MouseEvent& event) override;
    void mouseUp(const eacp::Graphics::MouseEvent& event) override;
    void mouseMoved(const eacp::Graphics::MouseEvent& event) override;
    void mouseExited(const eacp::Graphics::MouseEvent&) override;
    void mouseWheel(const eacp::Graphics::MouseEvent& event) override;

    ecode::WidgetHost host;

    ecode::FontSettings fontSettings {Look::chromeFont()};
    ecode::FontSettings builtFont;
    float builtAtScale = 1.f;

    float surfaceWidth = 0.f;
    float surfaceHeight = 0.f;

    std::optional<eacp::Sprites::SpriteRenderer> sprites;
    eacp::OwningPointer<eacp::Text::GlyphAtlas> atlas;
    std::optional<eacp::Text::GlyphRenderer> glyphs;
};
} // namespace Tour
