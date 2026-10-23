# 19. Anti-Aliasing

At the end of chapter 16 (Framebuffers) the edges of the cubes came out a
little jagged, more than they had in the chapters before it. That was not the
post-processing. It was the texture the scene was drawn into: a texture target
takes one sample per pixel, and one sample per pixel is exactly what produces
**aliasing**, the staircase along a sloping edge and the shimmer when it
moves. In this chapter you will see why that happens, how **supersampling**
and **multisampling** (MSAA) fix it and what each one costs, and how to switch
multisampling on in the two places a picture can be drawn: the window, which
has quietly had it since chapter 01, and an offscreen texture, which needs to
ask for it and then **resolve** the samples before anything reads the result.

The program draws the same scene twice, side by side: on the left with one
sample per pixel, on the right with four. The scene is chosen to be as hard as
possible on the rasterizer: a fan of long, very thin white spokes on a
near-black background, and a flat-coloured cube tumbling above them. A
magnifier in each half blows up the same small square so you can see
individual pixels.

## Why edges go jagged

Chapter 02 (Hello Triangle) described rasterization as working out "which
pixels the triangle covers". It is worth being precise about what "covers" means. The
rasterizer does not measure how much of a pixel a triangle overlaps. It tests
**one point**, the pixel's centre, and the answer is yes or no. A pixel whose
centre is inside gets the triangle's colour; a pixel whose centre is a hair
outside gets nothing, even if the triangle covers 49% of it.

```
  the edge you asked for                  what one test per pixel draws

                         ____----           .  .  .  .  .  .  #  #
                 ____----                   .  .  .  .  #  #  #  #
         ____----                           .  .  #  #  #  #  #  #
  ----       the triangle is below          #  #  #  #  #  #  #  #

  each pixel is all triangle (#) or all background (.), decided by
  which side of the edge its centre falls on
```

A smooth edge sampled at a grid of points turns into a staircase whose steps
are whole pixels, the **jaggies**. This is the same thing that happens in audio
when a signal is sampled too coarsely, and it has the same name: the true
signal (the edge) contains detail finer than the sample spacing, and that
detail does not vanish, it comes back disguised as something else.

Three disguises show up in this chapter's scene:

- **Staircases** along the cube's long straight edges, worst when an edge is
  nearly horizontal or nearly vertical, where each step is many pixels long.
- **Broken lines.** A spoke narrower than a pixel only produces a pixel
  where it happens to cross a pixel centre. Elsewhere it falls between
  centres and draws nothing, so a continuous line becomes a row of dashes.
  The magnifier on the left shows exactly this.
- **Crawling.** As things move, which pixels pass the test changes from frame
  to frame. The steps of a staircase march along the edge, and the dashes of a
  thin line flicker. Of the three this is often the most noticeable, and it is
  why aliasing looks worse in motion than in a screenshot.

The cure in every case is the same idea: take **more than one sample per
pixel** and let the pixel's colour be the average of what the samples saw. A
pixel that is half covered by a white spoke then becomes a 50% grey, which the
eye reads as "a thin line crossing here" rather than "nothing" or "a whole
pixel".

## Supersampling

The brute-force version is **supersampling** (SSAA): render the whole image at
a higher resolution, say twice the width and twice the height, and then shrink
it to the window by averaging each 2x2 block of pixels into one. Every pixel
of the result has seen four samples. It fixes everything, the edges and also
detail *inside* triangles, like a fine texture or a tiny specular highlight,
because every one of those samples ran the fragment shader.

That last part is also its problem. Four samples per pixel means four times as
many fragment shader runs, four times the colour memory and four times the
bandwidth to write it, to improve mostly the few pixels that sit on an edge.
The interior of a large triangle is one flat colour, and shading it four times
per pixel produces the same colour four times.

## Multisampling

**Multisampling** (MSAA) keeps the cheap part of supersampling and drops the
expensive part. Each pixel gets several **sample points**, typically 4,
spread inside it in a pattern chosen so that edges at every angle cross
different ones:

```
  one pixel, 1 sample               one pixel, 4 samples

  +-----------+                     +-----------+
  |           |                     |    x      |
  |     o     |                     |         x |
  |           |                     | x         |
  +-----------+                     |      x    |
                                    +-----------+
```

For every triangle, the rasterizer tests all four sample points for coverage
(and the depth test runs per sample too, against a depth buffer that also has
four values per pixel). But if any of the four is covered, the **fragment
shader runs only once** for that pixel, at the centre, and its colour is
written into just the samples the triangle covered. A pixel on an edge might
end up with three samples of white spoke and one sample of background.

When the pass is finished, the samples of each pixel are averaged into one
colour, the **resolve**. Three white samples and one black become a 75% grey.

```
  coverage per sample:   x x       colour per sample:   W W      resolve:
                         x .                            W B      (3 W + 1 B) / 4

  the fragment shader ran once, for the triangle that covered the three
```

So MSAA costs extra memory (four colour and depth values per pixel, until the
resolve) but almost no extra shading: a pixel in the middle of a triangle has
all four samples covered by the same triangle and runs the fragment shader
once, exactly as before. Only pixels on edges, where two or more triangles
share the samples, pay for more than one fragment shader run, one per
triangle that touches them.

The flip side is that MSAA only smooths **geometry edges**. Detail made by the
fragment shader, such as a texture's fine pattern or a sharp highlight, is
still computed once per pixel and still aliases. That job belongs to texture
filtering and mipmapping, not to multisampling. In this chapter's scene the
colours are flat, so every bit of aliasing is a geometry edge, and MSAA can fix
all of it.

## Multisampling the window

Here is the surprise: you have been multisampling since chapter 01. A
`GPUView` asks for **4 samples per pixel by default**. The window's pass
renders into a hidden 4-sample colour texture (and a 4-sample depth buffer, if
you called `setDepth`), and resolves it into the drawable the screen shows when
the pass ends. That is why every chapter has prepared its pipelines with

<!-- snippet: illustrative -->
```cpp
shader.prepare(sampleCount());
```

or set `pipeline.sampleCount = sampleCount()`. A pipeline is built for a
particular number of samples, and the draw is rejected if it does not match
the pass it draws into, so the pipeline asks the view what it is.

You change the count with `setSampleCount(n)`, before building the pipelines
that draw into the view. 1 turns multisampling off. On the GPU's side, 4 is a
count every device supports for ordinary formats; 2 and 8 are common but not
guaranteed.

## Multisampling a texture, and the resolve

A texture target is single-sampled unless you ask, which is what chapter 16
ran into. Asking takes one field:

<!-- snippet: illustrative -->
```cpp
auto descriptor = TextureDescriptor {};
descriptor.width = width;
descriptor.height = height;
descriptor.format = targetFormat;
descriptor.renderTarget = true;
descriptor.depth = true;
descriptor.sampleCount = 4;
```

With `sampleCount` above 1, the texture you get back grows a **multisampled
twin**: a second, hidden colour texture with four samples per pixel. A pass
that renders into the target really renders into the twin, and at the end of
the pass the twin is resolved into the target. The depth buffer that comes
with `depth = true` is made at the same count, since the depth test runs per
sample.

```
  frame.beginPass(target)                                the closing brace
         |                                                      |
         v                                                      v
  draws ---> [ twin: 4 samples per pixel ] ---- resolve ----> [ target: 1 per pixel ]
             [ depth: 4 per pixel        ]   (average each       what a shader samples,
                                              pixel's samples)    what read() reads
```

The texture you hold is always the resolved one. You sample it in a later pass
exactly as in chapter 16, with no idea it was ever multisampled. (In OpenGL
this is two framebuffers and a `glBlitFramebuffer` to copy one into the other;
learnopengl's chapter spends a while on it. Here the resolve is part of ending
the pass.)

The order matters for post-processing. The resolve happens at the end of the
scene's pass, *before* the post-processing pass samples the texture, so the
effects work on a picture whose edges are already smooth. A post-processing
shader cannot see individual samples; whatever it reads is already one colour
per pixel. If the scene were rendered single-sampled and the window
multisampled instead, as in chapter 16, the window's MSAA would have nothing to
do: a full-screen quad has its only edges at the borders of the screen.

Three rules come with it:

1. **Every pipeline drawing into the target needs the same sample count.**
   `RenderPipelineDescriptor::sampleCount = 4`, and the draw is rejected if it
   disagrees with the target. Drawing the same scene at two sample counts
   therefore means two pipelines.
2. **A count the GPU cannot do makes the texture invalid**, rather than
   silently giving you one sample per pixel (which would leave every pipeline
   built for the wrong number). `Device::shared().supportsSampleCount(n)` asks
   first.
3. **A multisampled texture is a render target and nothing else**: you never
   sample the twin, only the resolved texture, so nothing that reads it
   changes.

## Walking through the code

### The scene

The fan is 72 long, thin triangles lying flat on the xz plane, each one a
wedge from a point near the hub to two corners at the rim. A wedge is only
0.008 radians wide, so on screen even its wide end is about a pixel across
and it narrows to nothing towards the hub. That is the worst case for one
sample per pixel:

```cpp
for (auto i = 0; i < spokeCount; ++i)
{
    auto angle = 2.0f * pi * (float) i / (float) spokeCount;

    // White and pale blue alternate, so neighbouring spokes are told apart.
    auto color = i % 2 == 0 ? Vec3 {1.0f, 1.0f, 1.0f} : Vec3 {0.6f, 0.85f, 1.0f};

    auto point = [&](float radius, float a)
    { return Vec3 {radius * std::cos(a), 0.0f, radius * std::sin(a)}; };

    auto first = (std::uint16_t) mesh.vertices.size();

    mesh.vertices.push_back({point(innerRadius, angle), color});
    mesh.vertices.push_back({point(outerRadius, angle - halfWidth), color});
    mesh.vertices.push_back({point(outerRadius, angle + halfWidth), color});
    ...
}
```

The cube is chapter 08's cube with darker and lighter shades of one orange on
its faces. Its silhouette and the creases between its faces are long straight
edges that turn slowly through every slope. Both meshes are drawn with
chapter 08's one-matrix shader, unlit on purpose: lighting would put soft
gradients across the faces, and we want every edge as hard as possible.

### One scene per sample count

A pipeline is built for one sample count, so the scene's two shaders and the
texture they draw into are grouped into a `Scene`, made for a given count:

```cpp
struct Scene
{
    explicit Scene(int samples)
        : sampleCount(samples)
    {
        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount;
        pipeline.depth = true;
        pipeline.colorFormat = pixelFormatFor(targetFormat);

        fan.setVertices(fanMesh.vertices.data(), (int) fanMesh.vertices.size());
        fan.setIndices(fanMesh.indices.data(), (int) fanMesh.indices.size());
        fan.prepare(pipeline);

        pipeline.cullMode = CullMode::Back;
        cube.setVertices(cubeMesh.vertices.data(), (int) cubeMesh.vertices.size());
        cube.setIndices(cubeMesh.indices.data(), (int) cubeMesh.indices.size());
        cube.prepare(pipeline);
    }
```

Compared with chapter 16's scene pipeline, the only change is that
`sampleCount` is no longer hard-wired to 1. The fan is drawn without culling
(a flat sheet seen from above has no back to hide) and the cube with it.

The target is chapter 16's render target with one more field, set to the same
count the pipelines were built with:

```cpp
    void resize(int width, int height)
    {
        auto descriptor = TextureDescriptor {};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.format = targetFormat;
        descriptor.renderTarget = true;
        descriptor.depth = true; // multisampled too, at the same count
        descriptor.sampleCount = sampleCount;

        target.emplace(Device::shared().makeTexture(descriptor));
    }
```

Drawing is an ordinary pass into the target. Nothing in it mentions samples or
resolving: the pass ends at the closing brace of `draw`, and that is when the
resolve happens.

```cpp
    void draw(Frame& frame, float angle)
    {
        auto aspect = (float) target->width() / (float) target->height();
        auto view = Mat4::lookAt({0.0f, 2.2f, 3.6f}, {0.0f, 0.2f, 0.0f}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        auto pass =
            frame.beginPass(*target, {Graphics::Color {0.04f, 0.04f, 0.06f}});

        fan.modelViewProjection = viewProjection * Mat4::rotationY(angle * 0.25f);
        pass.draw(fan);

        cube.modelViewProjection =
            viewProjection * Mat4::translation({0.0f, 0.7f, 0.0f})
            * Mat4::rotationY(angle) * Mat4::rotationX(angle * 0.7f)
            * Mat4::scale(0.8f);
        pass.draw(cube);
    }
```

The view holds two scenes: one at 1 sample, and one at the count the right
half is currently showing. The second is an `optional` so that a click can
replace it with a scene at a different count:

```cpp
    Scene aliased {1};
    std::optional<Scene> smooth;
```

### Two panes, two viewports

Each scene texture is the size of half the window, and the window's single
pass draws them side by side with chapter 16's full-screen quad, using
**viewports** to put each one in its own half. A viewport remaps clip space
onto a rectangle of the target, so the same quad that covered the whole
window in chapter 16 now covers just the rectangle:

```cpp
        auto pass = frame.beginPass({Graphics::Color {0.5f, 0.5f, 0.5f}});

        pass.setViewport({0.0f, 0.0f, (float) pane, (float) height});
        pass.draw(leftComposite);

        pass.setViewport(
            {(float) (width - pane), 0.0f, (float) pane, (float) height});
        pass.draw(rightComposite);
```

The panes leave a gap of a few points between them, which keeps the pass's
grey clear colour as a divider. Each pane gets its own composite shader, since
each samples a different texture.

Notice what the view does *not* do. It calls `setSampleCount(1)`:

```cpp
        // The window only ever receives two pictures copied one texel per
        // pixel. Nothing drawn into it has an edge to smooth, so it does not
        // need the 4 samples a GPUView takes by default.
        setSampleCount(1);
```

The window's pass only draws two quads whose edges are the borders of the
panes, exactly on pixel boundaries. Four samples per pixel would cost memory
and smooth nothing. If the window kept its default 4, the left pane would
still be aliased: its picture was decided in its own single-sampled pass, and
copying it into a multisampled window copies the jaggies faithfully.

### The magnifier

The composite shader draws its texture one texel per pixel with nearest
filtering, like chapter 16's post shader, and adds an inset that blows up a
small square of the picture six times. It works in pixels of the texture, and
uses a helper that is 1 inside a square and 0 outside:

```cpp
// 1 inside the square of half-size `half` around `center`, 0 outside.
Float insideSquare(const Float2& p, const Float2& center, const Float& half)
{
    auto d = abs(p - center);
    return step(d.x(), half) * step(d.y(), half);
}
```

`step(edge, x)` is 1 when `x >= edge`, so each factor is 1 when the point is
within `half` of the centre along that axis. Inside the inset, the shader
reads from around the focus point instead, six times closer:

```cpp
        auto pixel = uv * size;
        auto sourceHalf = insetHalf / zoom;

        auto inInset = insideSquare(pixel, insetCenter, insetHalf);
        auto zoomed = focus + (pixel - insetCenter) / zoom;
        auto source = pixel + (zoomed - pixel) * inInset;

        auto color = sample(scene, source / size).xyz();
```

The last line before the sample is an *if* written as arithmetic: with
`inInset` 0 it is `pixel`, with `inInset` 1 it is `zoomed`. Nearest filtering
is what makes the magnifier honest. Each texel of the original becomes a
crisp 6x6 block, with no interpolation inventing in-between shades that
were never rendered. Every grey you see in the right-hand inset is a resolve,
not a filter.

A thin yellow frame marks both the inset and the square it magnifies, and a
row of yellow dots in the top-left corner shows each pane's sample count:
the shader runs a C++ loop over eight dot positions and `step` turns off the
ones past `samples`.

### Clicking through the sample counts

A click moves the right pane to the next sample count the GPU supports, out of
2, 4 and 8. A new count needs both a new pipeline and a new target, so the
click replaces the whole `Scene` and makes the next frame rebuild the
textures:

```cpp
    void mouseDown(const Graphics::MouseEvent&) override
    {
        countIndex = (countIndex + 1) % (int) counts.size();
        smooth.emplace(counts[countIndex]);
        paneWidth = 0; // forces resizeTargets() to build its texture
    }
```

The list of counts is made once, asking the device which ones it can do:

```cpp
    for (auto count: {2, 4, 8})
        if (Device::shared().supportsSampleCount(count))
            counts.push_back(count);
```

## What you should see

Two copies of the scene: a tumbling orange cube above a slowly turning fan of
thin white and pale blue spokes, on near-black. The left pane has one dot in
its corner, the right pane four.

At normal size the left pane already looks rougher: the cube's edges are
stepped and the spokes look scratchy, breaking up into dotted lines towards
the hub. The right pane's spokes are continuous all the way in and fade
smoothly into a fine grey haze where they crowd together, and the cube's edges
are clean.

The insets make it unmistakable. On the left, each spoke is a row of separate
bright dashes with black gaps between them, every pixel either fully lit or
fully dark. On the right, the same spokes are unbroken lines of pixels in
several shades between the spoke's colour and black: the pixels the spoke
only partly covered, resolved to how much of them it covered.

Watch them move: the left pane's dashes crawl along the spokes and the cube's
steps march along its edges, while the right pane stays steady.

Click, and the right pane switches to the next sample count. At 2 samples
(two dots) the in-between shades are coarser, only half-on or fully on; at 8,
if your GPU has it, they are finer still, though the step from 4 to 8 is much
smaller to the eye than the step from 1 to 4.

## Try it yourself

1. Go back to chapter 08 and add `setSampleCount(1);` as the first line of
   the view's constructor, then try 2. *Hint: the pipeline follows
   automatically because it was prepared with `sampleCount()`; the view must
   be told before the pipeline is built.*
2. Give chapter 16's offscreen target 4 samples, so its post-processing runs on
   a smooth picture. *Hint: two lines, one on the `TextureDescriptor` and one
   on the scene's `RenderPipelineDescriptor`. The post shader does not change
   at all.*
3. Remove `setSampleCount(1)` from this chapter's view, so the window has 4
   samples again. The left pane stays exactly as jagged. *Hint: the composite
   pipelines follow through `sampleCount()`; the aliasing was baked in by the
   left scene's own pass.*
4. Implement supersampling for the left pane: make its texture twice the
   pane's width and height, keep 1 sample, and in the composite shader average
   the four texels that land on each window pixel. Compare it with the 4x
   MSAA pane. *Hint: with `TextureFilter::Linear`, one sample exactly on the
   corner the four texels share returns their average. The two panes should
   look almost the same; the difference is how much work each took.*
5. Show where MSAA stops helping: give the cube's faces a fine stripe pattern
   computed in the fragment shader, for example
   `step(0.5f, fract(position.x() * 40.0f))` on the local position carried as a
   varying. *Hint: the stripes alias on both sides alike, because the
   fragment shader still runs once per pixel; only the cube's outline improves
   on the right.*
6. Ask for a sample count of 3. *Hint: the texture comes back invalid rather
   than quietly single-sampled; check it with `supportsSampleCount` first.*

## Further reading

- EACP GPU README, [Multisampling a target](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#multisampling-a-target): the multisampled twin, the resolve at the end of every pass, and why a refused count makes the texture invalid.
- EACP GPU README, [Viewport, and how it differs from a scissor](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#viewport-and-how-it-differs-from-a-scissor): split screen with one pass.
- learnopengl.com, [Anti Aliasing](https://learnopengl.com/Advanced-OpenGL/Anti-Aliasing), the same ideas with OpenGL's multisampled framebuffers and `glBlitFramebuffer`.
- Matt Pettineo, [A Quick Overview of MSAA](https://therealmjp.github.io/posts/msaa-overview/), for sample patterns, coverage and what happens at the resolve.

Previous: [18. Instancing](../18-Instancing/README.md) | Next: [20. Shadow Mapping](../20-ShadowMapping/README.md)
