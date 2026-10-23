# 01. Hello Window

Before you can draw a triangle, a texture or a lit teapot, you need somewhere to
draw it and a way to show the result. In this chapter you open a window and have
the GPU fill it with a single colour. That is the whole program, and it is about
forty lines with comments. Along the way you will learn what a frame is, what a
swapchain and vsync are, what a render pass does, and how EACP's `GPUView`,
`Frame`, `beginPass` and `runWindowedApp` fit together into the render loop
that every later chapter builds on.

## What a GPU draws into

A monitor shows a grid of pixels and refreshes the whole grid many times a
second: 60 times on a typical display, 120 on a ProMotion MacBook. Each picture
it shows is a **frame**. To animate anything, a program has to produce a new
image for the frame it wants to change, and hand it over in time.

The program does not draw straight onto the screen. If it did, you would see
half-finished pictures: the top of the image from this frame and the bottom from
the last one (an artefact called *tearing*). Instead the operating system gives
your window a small ring of off-screen images, usually two or three, called the
**swapchain**:

```
   your program                          the display
   ------------                          -----------
   draws into   +---------+              shows
   image B ---> | image A | --- on screen --->  [ picture ]
                +---------+
                | image B |  <- being drawn this frame
                +---------+
                | image C |  <- waiting / free
                +---------+

   present(B): "B is finished, show it next"
```

While the display shows one image, the GPU draws into another. When the drawing
is done, the program **presents** the image: it tells the system "this one is
ready". The swap does not happen instantly. It waits for **vsync** (vertical
synchronisation), the moment between two refreshes when the display starts
drawing a new picture from the top. Swapping only then means you never see two
frames mixed together.

So a frame, from the program's side, is: take a free image from the swapchain,
record GPU commands that draw into it, submit those commands, and present the
image so it appears at the next vsync.

## Render passes and clearing

The GPU does not take commands one at a time. You record a list of them (a
**command buffer**) and submit the whole list. Drawing commands are grouped into
**render passes**. A render pass names the image it draws into (its *target*)
and says what happens to that image when the pass starts: keep what was there,
or **clear** it to a solid colour.

Clearing is almost always what you want at the start of a frame. The image you
get from the swapchain still holds whatever was drawn into it a few frames ago,
and you are about to paint the new frame on top. Clearing is also cheap: GPUs
can fill a whole image with one colour far faster than they can draw it pixel by
pixel. In this chapter the clear is the only thing we do, so the clear colour is
the picture.

Colours on the GPU are four numbers between 0 and 1: red, green, blue and
**alpha** (opacity). `(0, 0, 0)` is black, `(1, 1, 1)` white, `(1, 0, 0)` pure
red, and `(0.2, 0.3, 0.3)`, which we use below, a dark teal.

## How EACP packages this

You do not handle swapchain images or command buffers directly. EACP wraps them
in three pieces:

- **`GPUView`** is a view (a rectangle inside a window) that owns a swapchain.
  You subclass it and override `render(Frame&)`.
- **`Frame`** is one frame's worth of work: the swapchain image being drawn into
  plus the command buffer that records into it. The view creates one, passes it
  to `render`, and when `render` returns the frame submits its commands and
  presents the image.
- **`RenderPass`**, returned by `frame.beginPass(...)`, is an open render pass on
  that image. Draw calls go through it. It ends when the object is destroyed.

And one helper:

- **`Graphics::runWindowedApp<View>()`** starts the application, constructs your
  view, puts it in a window and runs the event loop until the window is closed.

### When does `render` run?

`render` is called once per frame, but by default frames are only made when
something needs them. EACP's `GPUView` renders **on demand**: when the window
first appears, when it is resized, or when you call `repaint()` after changing
something. While nothing changes, no GPU work is submitted at all, and your
program sits idle. That is the right default for a tool or an editor, and for
this chapter, where the picture never changes.

A game or an animation wants a new frame at every display refresh instead. For
that a `GPUView` has a **continuous** mode, switched on with
`setContinuous(true)`, which calls `render` once per vsync. Chapter 04 (Uniforms)
covers it properly, together with animating things over time.

```
   on demand (default)                    continuous
   -------------------                    ----------
   window shown  -> render()              vsync -> update() -> render()
   resized       -> render()              vsync -> update() -> render()
   repaint()     -> render()              vsync -> update() -> render()
   (otherwise nothing)                    ...
```

## Walking through the code

The whole program lives in `Main.cpp`. It starts with the one header that brings
in the GPU module (and, through it, windows and views), a small header from this
series that we will meet at the end of the file, and two `using` directives so
we can write `GPUView` rather than `eacp::GPU::GPUView`:

```cpp
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

using namespace eacp;
using namespace GPU;

// Each chapter's code lives in a namespace named after the chapter.
namespace HelloWindow
{
```

The namespace keeps every chapter's names (and later chapters have many
`Vertex` structs and `makeCube` functions) out of each other's way, so all the
chapters can live side by side in one program.

Next comes our view. It derives from `GPUView` and overrides one function:

```cpp
struct HelloWindowView final : GPUView
{
    // Called once per frame. By default that is only when the view needs
    // redrawing (it was just shown, resized or asked to repaint), not every
    // display refresh, so an idle window costs no GPU work at all.
    void render(Frame& frame) override
    {
```

`render` receives the `Frame` for this frame. By the time it is called, EACP
has already taken a free image from the swapchain and opened a command buffer
for it.

```cpp
        auto pass = frame.beginPass({.clearColor = clearColor});
```

`beginPass` takes a `RenderPassDescriptor`, which says how the pass begins. We
set only `clearColor`; its other field that matters here, `clear`, is `true` by
default, so the pass clears the image to our colour. The designated initializer
`{.clearColor = ...}` is plain C++20 and leaves every other field at its
default. If you call `frame.beginPass()` with no argument at all, the pass
clears to black.

We keep the result in `pass` even though we draw nothing with it. A
`RenderPass` ends its pass when it is destroyed, so the variable marks where
the pass is open: from here to the closing brace of `render`. In the next
chapter, the draw call goes right after this line.

```cpp
        // When `pass` goes out of scope the pass ends, and when render()
        // returns the frame commits its commands and presents the image: it
        // is shown at the next vsync, the moment the display starts its next
        // refresh.
    }
```

Nothing else is needed: no `present()` or `submit()` call. The `Frame` does it
for you when `render` returns. EACP uses C++ object lifetimes (RAII) for all of
these begin/end pairs, so you cannot forget to end a pass or present a frame.

The colour is a member, so it is easy to change:

```cpp
    Graphics::Color clearColor {0.20f, 0.30f, 0.30f};
```

`Graphics::Color` takes red, green, blue and an optional alpha that defaults to
1 (fully opaque). A `GPUView` is opaque by default, so the alpha of the clear
colour does not show through to the desktop anyway.

Finally, after the namespace closes, one line hands our view type to
`runWindowedApp`, which does the rest:

```cpp
} // namespace HelloWindow

// LEARNEACP_CHAPTER is the program's main(): it calls runWindowedApp, which
// creates the application, constructs the view, puts it in a window and runs
// the event loop until the window is closed.
LEARNEACP_CHAPTER(HelloWindow::HelloWindowView)
```

`LEARNEACP_CHAPTER` is the series' own macro from `LearnEACP/Chapter.h`, and
every chapter ends with it. Built as this chapter's program, it is nothing more
than the `main` you would write by hand:

```cpp
int main()
{
    return Graphics::runWindowedApp<HelloWindow::HelloWindowView>();
}
```

The macro exists so the same file can also be built into a program that shows
every chapter: there, instead of `main`, it adds the chapter to that program's
list. Your own projects can simply write the `main` above.

Build and run the `HelloWindow` target and you should get a window filled with
dark teal. Resize it: the colour fills the new size, because each resize is a
new frame and each frame clears the whole image.

## Try it yourself

1. Change the clear colour to your favourite colour. *Hint: each channel is 0
   to 1, not 0 to 255; divide by 255 if you start from a colour picker.*
2. Replace the argument to `beginPass` with nothing at all, `frame.beginPass()`.
   What colour do you get, and why? *Hint: look at the default of
   `RenderPassDescriptor::clearColor` in `Frame.h`.*
3. Give the window a title and a starting size. *Hint: replace the
   `LEARNEACP_CHAPTER` line with the hand-written `main` shown above;
   `runWindowedApp` takes a `Graphics::WindowOptions` as its first argument,
   so look at its fields in `Window.h`.*
4. Make the colour slowly cycle. *Hint: call `setContinuous(true)` in a
   constructor so `render` runs every vsync, keep a frame counter, and compute
   the red channel from `0.5f + 0.5f * std::sin(counter * 0.02f)`. Chapter 04 (Uniforms)
   shows the proper way, with real time instead of a counter.*

## Further reading

- EACP GPU README, [The pieces](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#the-pieces):
  one line on each of `Device`, `GPUView`, `Frame` and `RenderPass`.
- `Lib/eacp/GPU/View/GPUView.h` in EACP: the comments explain on-demand versus
  continuous rendering, `setMaxFps` and `framesInFlight` (the swapchain's size).
- learnopengl.com, [Hello Window](https://learnopengl.com/Getting-started/Hello-Window):
  the same first step in OpenGL, where you write the render loop by hand.
- Wikipedia, [Multiple buffering](https://en.wikipedia.org/wiki/Multiple_buffering)
  and [Screen tearing](https://en.wikipedia.org/wiki/Screen_tearing), for more
  on why swapchains and vsync exist.

Next: [02. Hello Triangle](../02-HelloTriangle/README.md)
