// Chapter 01: Hello Window.
//
// The smallest GPU program there is: open a window and ask the GPU to fill it
// with one colour. No shaders, no geometry - just the frame loop every later
// chapter builds on.
#include <eacp/GPU/GPU.h>

using namespace eacp;
using namespace GPU;

// A GPUView is a view (a rectangle inside a window) that owns a swapchain: a
// small ring of images the GPU draws into while the display shows another one.
// Override render() and the view calls it whenever a new frame is needed.
struct HelloWindowView final : GPUView
{
    // Called once per frame. By default that is only when the view needs
    // redrawing (it was just shown, resized or asked to repaint), not every
    // display refresh, so an idle window costs no GPU work at all.
    void render(Frame& frame) override
    {
        // A frame is one image's worth of GPU commands. beginPass() opens a
        // render pass on it: the pass picks the image up, clears every pixel
        // to clearColor, and is where draws would go. We have nothing to draw
        // yet, so the clear is the whole picture.
        //
        // Colours are red, green, blue (and an optional alpha), each 0 to 1.
        auto pass = frame.beginPass({.clearColor = clearColor});

        // When `pass` goes out of scope the pass ends, and when render()
        // returns the frame commits its commands and presents the image: it
        // is shown at the next vsync, the moment the display starts its next
        // refresh.
    }

    Graphics::Color clearColor {0.20f, 0.30f, 0.30f};
};

// runWindowedApp creates the application, constructs the view, puts it in a
// window and runs the event loop until the window is closed.
int main()
{
    return Graphics::runWindowedApp<HelloWindowView>();
}
