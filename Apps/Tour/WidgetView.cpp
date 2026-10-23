#include "WidgetView.h"

using namespace eacp;

namespace Tour
{
WidgetView::WidgetView()
{
    // The atlas's text is antialiased already, so multisampling buys nothing.
    setSampleCount(1);
    setHandlesMouseEvents(true);
}

void WidgetView::setRoot(ecode::Widget& root)
{
    host.setRoot(root);

    // The host attaches nothing itself; this is what turns a widget's
    // repaint() into a frame.
    root.onRepaintNeeded = [this] { repaint(); };

    host.setBounds(getLocalBounds());
    repaint();
}

void WidgetView::ensureRenderer()
{
    const auto scale = backingScale();

    if (atlas && builtAtScale == scale && builtFont == fontSettings)
        return;

    auto built = ecode::makeGlyphAtlas(fontSettings, scale);

    if (!built)
        return;

    atlas = std::move(built);
    builtFont = fontSettings;
    builtAtScale = scale;

    if (!glyphs)
        glyphs.emplace();
}

void WidgetView::ensureSurface()
{
    const auto bounds = getLocalBounds();

    if (bounds.w <= 0.f || bounds.h <= 0.f)
        return;

    if (sprites && surfaceWidth == bounds.w && surfaceHeight == bounds.h)
        return;

    sprites.emplace(Graphics::Point {bounds.w, bounds.h}, sampleCount());

    surfaceWidth = bounds.w;
    surfaceHeight = bounds.h;

    if (glyphs)
        glyphs->setViewportSize({bounds.w, bounds.h});

    host.setBounds(bounds);
}

void WidgetView::resized()
{
    GPUView::resized();

    ensureSurface();
    repaint();
}

void WidgetView::backingScaleChanged()
{
    GPUView::backingScaleChanged();

    ensureRenderer();
    repaint();
}

void WidgetView::render(GPU::Frame& frame)
{
    ensureSurface();
    ensureRenderer();

    auto pass = frame.beginPass({Look::panel});

    if (!sprites || !atlas || !glyphs)
        return;

    const auto bounds = getLocalBounds();

    glyphs->setViewportSize({bounds.w, bounds.h});

    // Every glyph the frame needs is rasterized before the first draw, then
    // uploaded once: uploading mid-pass would change a texture earlier draws
    // have already bound.
    host.prepare(*atlas);
    atlas->commit();

    auto context =
        ecode::PaintContext {pass, *sprites, *glyphs, *atlas, bounds, builtAtScale};

    host.paint(context);
}

void WidgetView::mouseDown(const Graphics::MouseEvent& event)
{
    host.mouseDown(event);
}

void WidgetView::mouseDragged(const Graphics::MouseEvent& event)
{
    host.mouseDragged(event);
}

void WidgetView::mouseUp(const Graphics::MouseEvent& event)
{
    host.mouseUp(event);
    setMouseCursor(host.cursorAt(event.pos));
}

void WidgetView::mouseMoved(const Graphics::MouseEvent& event)
{
    host.mouseMoved(event);
    setMouseCursor(host.cursorAt(event.pos));
}

void WidgetView::mouseExited(const Graphics::MouseEvent&)
{
    host.mouseExited();
}

void WidgetView::mouseWheel(const Graphics::MouseEvent& event)
{
    host.mouseWheel(event);
}
} // namespace Tour
