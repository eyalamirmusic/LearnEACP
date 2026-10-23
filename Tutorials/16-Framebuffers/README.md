# 16. Framebuffers

Every chapter so far has drawn straight into the window. That is fine until you
want to do something to the *finished picture*: turn it grayscale, blur it,
outline every edge, or simply look at it as an image. A fragment shader only
ever sees its own fragment, never the pixels around it, so none of that can
happen while the scene is being drawn. The answer is to draw the scene
somewhere else first: into a texture of our own. In OpenGL that "somewhere" is
a **framebuffer object**, a bundle of a colour texture and a depth buffer you
can render into instead of the screen. In this chapter you will render the lit
cubes from chapter 09 (Lighting), now standing on a floor, into such an
**offscreen target**, then draw that texture over the whole window with a
second, **post-processing** shader. Six effects take turns: the plain image,
inversion, grayscale, and three **convolution kernels** (sharpen, blur and edge
detection) that read a 3x3 block of neighbouring texels.

## What a framebuffer is

The window you draw into is a set of images the GPU writes: a colour image (the
drawable that ends up on screen) and, since chapter 08 (Coordinate Systems), a
depth buffer beside it. A render pass names those images as its
**attachments**: "the fragments of this pass go here, and are depth-tested
against that".

Nothing about that requires the colour image to be on screen. A texture can be
an attachment too, as long as it was created able to be one. Draw into it in
one pass, and in a later pass it is just a texture, which you can `sample()`
like the checkerboard from chapter 06 (Textures):

```
  pass 1: into the texture                    pass 2: into the window

  cubes + floor --LitShader--> [ colour ]     full-screen quad --PostShader--> drawable
                               [ depth  ]            ^
                                    |                | sample(scene, uv)
                                    +----------------+
```

OpenGL makes you assemble this by hand: generate a framebuffer object, attach
a texture to its colour slot, create a renderbuffer for depth and stencil,
attach that, check the framebuffer is "complete", and remember to bind and
unbind it around your draws. EACP folds the whole bundle into the texture:

- `TextureDescriptor::renderTarget = true` makes a texture a pass can draw
  into.
- `TextureDescriptor::depth = true` gives it a depth buffer of its own,
  created with it and destroyed with it.
- `frame.beginPass(texture, ...)` begins a pass whose attachments are that
  texture and its depth buffer, instead of the window's.

There is no separate object to keep alive and no "currently bound framebuffer"
to forget to reset. Which target a pass draws into is simply an argument to
the pass.

### Passes are ordered

Both passes are recorded on the one `Frame` that `render()` is given, and the
GPU runs the passes of a frame in the order they were begun. So by the time
pass 2 samples the texture, pass 1 has finished writing it, with no waiting or
fencing on your part. The one rule is that a pass cannot sample the texture it
is drawing into: a texture is either the thing being written or the thing
being read, never both at once. (Effects that feed a picture back into itself,
like motion trails, use two textures and swap them every frame.)

### The pipeline has to match its target

Back in chapter 02 (Hello Triangle), `prepare()` built a pipeline for the
window. A pipeline bakes in the *shape* of what it draws into: the colour
format, the sample count, and whether there is a depth buffer. Our scene now
draws into a texture, so its pipeline describes the texture instead:

- the texture's colour format, through `pixelFormatFor(format)`. The window
  happens to be BGRA; our texture is RGBA, and a pipeline built for one does
  not draw into the other;
- one sample: a texture target is single-sampled unless you ask otherwise, so
  this scene has no multisampling (chapter 19 is about getting it back);
- `depth = true`, because the texture has a depth buffer.

Notice what this does to depth. In chapters 08 and 09 the *view* owned the
depth buffer, which is why they called `setDepth(true)`. Here the scene draws
into the texture, whose depth buffer comes with it, and the window only ever
receives one flat quad that has nothing to hide behind. So this view never
calls `setDepth` at all.

## The full-screen quad

Pass 2 needs geometry that covers every pixel of the window exactly once. The
simplest is a quad from clip-space (-1, -1) to (1, 1), with no matrices at all:
its corners already are the corners of the screen. Each corner also carries
the texture coordinate of the same corner of the image.

Remember from chapter 06 that UV (0, 0) is the **top-left** texel, while clip
space has y pointing **up**. So the top-left corner of the quad, (-1, 1),
takes UV (0, 0), and the bottom-left, (-1, -1), takes (0, 1). Pass 1 wrote the
top row of the scene into row 0 of the texture, exactly where this reads it
back, so the picture comes out the right way up with no flip anywhere else.
(learnopengl's version has its UVs the other way round because OpenGL counts
texture rows from the bottom. If your post-processed image is upside down,
this is the line to check.)

Since the quad maps one texel onto one pixel, the shader samples with
**nearest** filtering: there is nothing between texels to blend.

## Keeping the target the size of the view

A texture's size is fixed when it is created. If the window grows and the
texture does not, the image is stretched and blurry; if the texture is a
different shape from the window, the scene is squashed. So the target has to
follow the view: whenever the view's size changes, make a new texture of the
new size.

The size that matters is in **pixels**, not points. On a Retina display a
window 640 points wide is 1280 pixels wide, and a 640-texel target would be
magnified 2x. `frame.pixelSize()` gives the size of this frame's drawable in
pixels, so each frame we compare it with the size we last built for and only
rebuild on a change. Rebuilding is cheap but not free, so it is worth not
doing it 60 times a second.

## Post-processing effects

With the scene in a texture, the post shader can compute each output pixel from
any texels it likes. The simplest effects use only the texel under the pixel.

**Inversion** subtracts each channel from 1: black becomes white, the blue sky
becomes brown, orange becomes pale blue.

**Grayscale** replaces the colour with its brightness. The plain average
`(r + g + b) / 3` works, but the eye is far more sensitive to green than to
blue, so a pure blue looks much darker than a pure green of the same value.
Weighting the channels by that sensitivity (0.2126, 0.7152 and 0.0722, the
weights of the Rec. 709 standard) gives a gray that keeps the brightness you
actually perceived. The weighted sum is just a `dot` product with the weights.

### Kernels: looking at the neighbours

The more interesting effects need the texels *around* the current one. A
**convolution kernel** is a small grid of weights, here 3x3, laid over the
image centred on the current texel. Each of the nine texels underneath is
multiplied by its weight, and the nine products are added up:

```
      neighbouring texels          kernel            result

     a  b  c                    k0 k1 k2
     d  e  f        (x)         k3 k4 k5     =   a*k0 + b*k1 + ... + i*k8
     g  h  i                    k6 k7 k8
```

The kernel decides the effect entirely:

```
     sharpen              blur (1/16 of)         edge detection

   -1  -1  -1             1   2   1               1   1   1
   -1   9  -1             2   4   2               1  -8   1
   -1  -1  -1             1   2   1               1   1   1
```

- **Sharpen** weights the centre heavily and subtracts the neighbours. Where
  the image is flat, the neighbours cancel eight ninths of the centre and the
  colour is unchanged (the weights sum to 1). Across an edge, the difference
  between the centre and its surroundings is exaggerated, so every edge gets a
  bright and a dark fringe.
- **Blur** is a weighted average: every output texel becomes a blend of its
  neighbourhood, more of the centre than the corners. The weights sum to 1 so
  the image keeps its brightness. Bigger kernels, or several blur passes in a
  row, blur further.
- **Edge detection** weights the neighbours positively and the centre by
  minus their total, so the weights sum to **0**: a flat region comes out
  black, and only where the texels differ, at the edges, does anything survive.
  The checkered floor becomes a grid of lines and each cube a wireframe of its
  silhouette and face edges.

Which texel is "the next one over"? UV coordinates run 0 to 1 across the whole
image, so one texel is `1 / width` across and `1 / height` down. That
**texel offset** depends on the target's size, so it is a uniform recomputed
whenever the target is rebuilt. We actually step one logical *point* rather
than one pixel (two texels on a Retina display), so the effects look the same
on any screen.

At the border, some neighbours fall outside the image. The sampler's **clamp**
address mode answers those with the nearest edge texel; repeat would wrap round
and pull in texels from the opposite side, which shows up as a thin line of the
wrong colour along each edge.

## Walking through the code

The vertex is chapter 09's position and normal, and the cube mesh comes from
the same `makeCube()`. The quad has a vertex type of its own:

```cpp
// The full-screen quad: a clip-space position and the texture coordinate that
// reads the same point of the offscreen image.
struct QuadVertex
{
    Vec2 position;
    Vec2 uv;
};
```

And its four corners, with the UVs flipped against clip space as described
above:

```cpp
// Clip space has y up, but row 0 of a texture is its top, so the corner at
// clip-space y = +1 reads v = 0. No flip is needed anywhere else: the offscreen
// pass writes its top row where this reads it back.
const QuadVertex screenQuad[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}}, // bottom left
    {{1.0f, -1.0f}, {1.0f, 1.0f}}, // bottom right
    {{1.0f, 1.0f}, {1.0f, 0.0f}}, // top right
    {{-1.0f, 1.0f}, {0.0f, 0.0f}}, // top left
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};
```

### The scene shader

`LitShader` is the Phong shader from chapter 09 with one addition. The floor is
a large flat surface, which gives the effects very little to do, so the shader
can paint a checkerboard onto it. `floor()` of the world x and z picks out
which unit square the fragment is in, and adding them and taking `mod 2` gives
0 and 1 alternating like the squares of a chessboard. The `checker` uniform is
1 for the floor and 0 for the cubes, which keep their plain colour:

```cpp
        // 0 or 1, alternating from one unit square to the next.
        auto square =
            mod(floor(fragmentPosition.x()) + floor(fragmentPosition.z()), 2.0f);
        auto albedo = objectColor * (1.0f - checker * 0.45f * square);

        setFragment(float4((ambient + diffuse) * albedo + specular, 1.0f));
```

The rest is exactly chapter 09's lighting, so it is not repeated here.

### The post shader

The effects are listed in an enum, and the three kernels are plain C++ arrays,
written row by row from the top-left neighbour:

```cpp
enum class Effect
{
    None,
    Inversion,
    Grayscale,
    Sharpen,
    Blur,
    EdgeDetection,
};

constexpr auto effectCount = 6;

// 3x3 convolution kernels, row by row from the top-left neighbour. The sharpen
// and edge kernels sum to 1 and 0; the blur sums to 1 (16 sixteenths).
using Kernel = std::array<float, 9>;

constexpr Kernel sharpenKernel = {-1, -1, -1, -1, 9, -1, -1, -1, -1};

constexpr Kernel blurKernel = {1.0f / 16,
                               2.0f / 16,
                               1.0f / 16,
                               2.0f / 16,
                               4.0f / 16,
                               2.0f / 16,
                               1.0f / 16,
                               2.0f / 16,
                               1.0f / 16};

constexpr Kernel edgeKernel = {1, 1, 1, 1, -8, 1, 1, 1, 1};
```

How do we switch between effects? One way is a uniform holding the effect
number and a chain of `select`s in the shader, which works but makes every
pixel compute every effect. Here we use something the EDSL makes easy instead.
`define()` is ordinary C++ that runs once, when the shader is compiled, and
*records* the operations it is asked for. So C++ can decide *which* operations
to record. `PostShader` takes an `Effect` in its constructor, and `define()`
`switch`es on it: each `PostShader` we construct compiles into a different
shader containing one effect and nothing else.

```cpp
struct PostShader final : ShaderProgram
{
    explicit PostShader(Effect effectToUse)
        : effect(effectToUse)
    {
        // The image is drawn one texel per pixel, so nearest filtering is
        // exact. Clamp makes the kernels repeat the border texel instead of
        // wrapping round to the opposite edge.
        scene.sampling = {TextureFilter::Nearest, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));

        setPosition(float4(position, 0.0f, 1.0f));
        setFragment(float4(apply(uv), 1.0f));
    }
```

The vertex stage is as simple as chapter 02's: the quad's position is already
in clip space. `effect` has to be set before `compile()`, because `compile()`
is what calls `define()`, exactly like the sampler from chapter 06.

`apply` is where the effects live. Notice the C++ `switch` and the shader
operations side by side: the `switch` runs in C++, and only the branch it
picks is recorded. Inversion's `1.0f - color` broadcasts the 1 across the
three channels, and grayscale's `dot` with the weights is a single number we
copy into all three:

```cpp
    Float3 apply(const Float2& uv)
    {
        auto color = sample(scene, uv).xyz();

        switch (effect)
        {
            case Effect::None:
                return color;

            case Effect::Inversion:
                return 1.0f - color;

            case Effect::Grayscale:
            {
                // Weighted by how bright each primary looks to the eye: green
                // most, blue least.
                auto luma = dot(color, float3(constant(0.2126f), 0.7152f, 0.0722f));
                return float3(luma, luma, luma);
            }

            case Effect::Sharpen:
                return convolve(uv, sharpenKernel);

            case Effect::Blur:
                return convolve(uv, blurKernel);

            case Effect::EdgeDetection:
                return convolve(uv, edgeKernel);
        }

        return color;
    }
```

(`float3(constant(0.2126f), ...)` is the literal-vector rule from earlier
chapters: a vector made only of C++ literals needs one of them wrapped in
`constant()`.)

`convolve` is the kernel formula. The two loops are C++ too: they run nine
times while the graph is being recorded, and leave behind nine `sample()`
calls, each at its own offset and with its weight baked in as a literal. The
GPU never sees a loop or the kernel array. The offset is a neighbour step in
x and y, `-1`, `0` or `+1` times the `texelOffset` uniform. `row` grows
downwards and so does v, so row 0 of the kernel is the row of texels above:

```cpp
    // Nine samples around uv, each weighted by its kernel entry and summed.
    // The loops are C++, so they run while the graph is recorded and leave
    // nine sample() calls in the shader, with the weights as literals.
    Float3 convolve(const Float2& uv, const Kernel& kernel)
    {
        auto sum = float3(constant(0.0f), 0.0f, 0.0f);

        for (auto row = 0; row < 3; ++row)
        {
            for (auto column = 0; column < 3; ++column)
            {
                // -1, 0 or +1 neighbour steps; v grows downwards, like row.
                auto dx = static_cast<float>(column - 1);
                auto dy = static_cast<float>(row - 1);
                auto offset = float2(texelOffset.x() * dx, texelOffset.y() * dy);

                sum = sum
                      + sample(scene, uv + offset).xyz() * kernel[row * 3 + column];
            }
        }

        return sum;
    }

    Effect effect;

    Uniform<Texture2D> scene;
    Uniform<Float2> texelOffset; // one neighbour step, in UV units

    EACP_SHADER(scene, texelOffset)
};
```

`sum = sum + ...` is plain C++ assignment of a handle: each time round, `sum`
is made to refer to a new "add" node in the graph. It is not a variable the
shader updates at run time.

### Preparing the two kinds of pipeline

The view's constructor prepares the scene's pipeline for the *texture*, as
described in "The pipeline has to match its target", and each post shader's
for the *window*, with the plain `prepare(sampleCount())` from chapter 02:

```cpp
struct FramebuffersView final : GPUView
{
    FramebuffersView()
    {
        // The pipeline that draws into the offscreen texture. It must match
        // that target, not the window: one sample, the texture's colour format,
        // and a depth attachment because the texture will have one.
        auto scenePipeline = RenderPipelineDescriptor {};
        scenePipeline.sampleCount = 1;
        scenePipeline.depth = true;
        scenePipeline.cullMode = CullMode::Back;
        scenePipeline.colorFormat = pixelFormatFor(targetFormat);

        litShader.setVertices(cube.vertices);
        litShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        litShader.prepare(scenePipeline);

        // The quad draws into the window, which has no depth buffer: a
        // full-screen image has nothing to hide behind.
        for (auto& post: postShaders)
        {
            post.setVertices(screenQuad);
            post.setIndices(quadIndices);
            post.prepare(sampleCount());
        }

        setHandlesMouseEvents(true);
        setContinuous(true);
    }
```

The members at the bottom of the view hold the six post shaders, one per
effect, and the target. The target is a `std::optional<Texture>`, because
there is no texture until the first frame tells us how big to make it:

```cpp
    static constexpr auto targetFormat = TextureFormat::RGBA8Unorm;
    static constexpr auto secondsPerEffect = 3.0f;

    CubeMesh cube = makeCube();
    LitShader litShader;

    // Declared before the shaders that point at it, so it outlives them.
    std::optional<Texture> target;
    int targetWidth = 0;
    int targetHeight = 0;

    std::array<PostShader, effectCount> postShaders {
        PostShader {Effect::None},
        PostShader {Effect::Inversion},
        PostShader {Effect::Grayscale},
        PostShader {Effect::Sharpen},
        PostShader {Effect::Blur},
        PostShader {Effect::EdgeDetection}};
```

### Creating and recreating the target

`resizeTarget` builds the texture: `renderTarget` so a pass can draw into it,
`depth` so that pass has a depth buffer. It returns early when nothing has
changed. `target.emplace(...)` destroys the old texture, if there was one, and
puts the new one in its place.

A shader's texture uniform only *points* at a texture (chapter 06), so after a
rebuild every post shader is pointed at the new one, and given the texel
offset for the new size:

```cpp
    // A texture's size is fixed when it is made, so a new view size means a
    // new texture. Called from render() with the size of this frame's
    // drawable, in pixels, and its pixels per logical point.
    void resizeTarget(int width, int height, float scale)
    {
        if (target && width == targetWidth && height == targetHeight)
            return;

        targetWidth = width;
        targetHeight = height;

        auto descriptor = TextureDescriptor {};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.format = targetFormat;
        descriptor.renderTarget = true; // a pass can draw into it
        descriptor.depth = true; // and that pass gets a depth buffer of its own

        target.emplace(Device::shared().makeTexture(descriptor));

        // One neighbour step is one logical point: a texel on a standard
        // display, two on a Retina one, so the effects look the same on both.
        for (auto& post: postShaders)
        {
            post.scene = *target;
            post.texelOffset = Vec2 {scale / (float) width, scale / (float) height};
        }
    }
```

### Cycling through the effects

`update` turns the camera slowly and moves on to the next effect every three
seconds. A mouse click moves on straight away: `GPUView` is a view like any
other, so overriding `mouseDown` hears about clicks, as long as the view has
asked for mouse events. A view ignores the mouse by default, which is why the
constructor calls `setHandlesMouseEvents(true)`, as chapter 11 (Camera) did.

```cpp
    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        cameraAngle += 0.15f * delta;
        effectTime += delta;

        if (effectTime > secondsPerEffect)
            nextEffect();
    }

    void mouseDown(const Graphics::MouseEvent&) override { nextEffect(); }

    void nextEffect()
    {
        effectIndex = (effectIndex + 1) % effectCount;
        effectTime = 0.0f;
    }
```

### Two passes per frame

`render` is the whole chapter in a dozen lines. It reads the drawable's pixel
size off the frame, makes sure the target matches it, and then records the two
passes. The first is begun with the texture as its target and the sky colour
as its clear colour, and lives inside its own braces: a pass ends when it goes
out of scope, and it has to end before the second pass can sample what it
drew. The aspect ratio comes from the same pixel size, so the projection
always matches the texture the scene lands in.

The second pass is the familiar `frame.beginPass()` into the window. It
draws a single quad, through whichever post shader is current:

```cpp
    void render(Frame& frame) override
    {
        auto size = frame.pixelSize();
        auto width = (int) size.x;
        auto height = (int) size.y;

        if (width <= 0 || height <= 0)
            return;

        resizeTarget(width, height, frame.backingScale());

        // First pass: the scene, into the texture. The pass ends at the
        // closing brace, and only then may a later pass sample what it drew.
        {
            auto pass =
                frame.beginPass(*target, {Graphics::Color {0.55f, 0.7f, 0.85f}});
            drawScene(pass, (float) width / (float) height);
        }

        // Second pass: the texture, through the current effect, into the window.
        auto pass = frame.beginPass();
        pass.draw(postShaders[effectIndex]);
    }
```

`drawScene` sets the camera and light once, then draws four objects with the
one `LitShader`, changing its model matrix, colour and checker flag before each
draw. A draw copies the shader's current uniform values into the pass at the
moment it is recorded, so the next object's values cannot overwrite the last
one's. The floor is the unit cube again, squashed to a thin slab 12 units
across, whose top face sits at y = 0. Its scale is non-uniform, so it goes
through chapter 09's normal matrix as a matter of course (for a cube's
axis-aligned faces it happens not to matter, as chapter 09 noted):

```cpp
        auto drawObject = [&](const Mat4& model, Vec3 color, float checker)
        {
            litShader.model = model;
            litShader.normalMatrix = model.inverted().transposed();
            litShader.objectColor = color;
            litShader.checker = checker;
            pass.draw(litShader);
        };

        // The floor: a cube flattened to a slab whose top face is at y = 0.
        drawObject(Mat4::translation({0.0f, -0.05f, 0.0f})
                       * Mat4::scale({12.0f, 0.1f, 12.0f}),
                   {0.85f, 0.85f, 0.8f},
                   1.0f);
```

## What you should see

Three cubes, orange, blue and a large green one, on a gray-and-cream
checkerboard under a pale blue sky, with the camera slowly circling them. For
the first three seconds the image is untouched, and it looks exactly like a
scene drawn straight to the window would. That is the first thing worth
checking: the detour through a texture should be invisible. Then, every three
seconds (or on every click):

1. **Inversion**: a brown sky, a purple cube, a light-blue one and a peach one,
   on a floor whose light squares have turned dark.
2. **Grayscale**: the same scene in shades of gray, the green cube brighter
   than the orange one although they looked similar in colour.
3. **Sharpen**: crisp, haloed edges, with a thin bright fringe along each
   cube's outline and along the checkerboard lines.
4. **Blur**: the whole image softened slightly, edges a couple of pixels wide.
5. **Edge detection**: black everywhere except a glowing outline of every
   edge: the floor's squares, the floor's far rim and each cube's faces.

Then back to the plain image. Resize the window and the image stays sharp and
undistorted, since a new target is made for every new size.

The edges in this chapter are slightly jagged, more than in chapter 09. That
is the single-sampled texture: there is no multisampling in the target.
Chapter 19 (Anti-Aliasing) is about getting it back.

## Try it yourself

1. Make the effect a **split screen**: show the plain image on the left half
   of the window and the current effect on the right. *Hint: in `apply`,
   `select(uv.x() < 0.5f, color, effectColor)` picks per pixel; a thin
   divider line is one more `select`.*
2. Blur harder by multiplying `texelOffset` by 3 or 4 in `resizeTarget`. What
   goes wrong with large steps? *Hint: the kernel skips the texels between its
   samples, so instead of a smooth blur you see three ghosted copies of each
   edge.*
3. Add an **emboss** effect with the kernel `{-2, -1, 0, -1, 1, 1, 0, 1, 2}`.
   *Hint: add an `Effect`, a `Kernel`, a `case`, and one more `PostShader` in
   the array; remember `effectCount`.*
4. Make the target a quarter of the window's size in each direction (keep the
   window-sized aspect ratio for the projection). The image becomes blocky.
   Now change the post shader's filter to `TextureFilter::Linear`. *Hint: this
   is how games render at a lower resolution and scale up, and why the target
   size and the window size are separate numbers.*
5. Combine two effects: grayscale *then* edge detection, so the edges are
   white instead of coloured. *Hint: either convert to gray inside `convolve`
   before weighting each sample, or render the grayscale image into a second
   texture and run the edge kernel over that, a third pass.*

## Further reading

- EACP GPU README, [Rendering into a texture](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#rendering-into-a-texture): a render target as a pass on the frame you were given, and why passes need no fence between them.
- EACP GPU README, [Depth](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#depth): a target's own depth buffer, and why the pipeline and the target must agree about it.
- EACP GPU README, [Sampling](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#sampling): why the sampler belongs to the shader.
- `Apps/GPU/DepthSampling` in EACP renders a scene into a target and then reads both its colour and its depth in later passes.
- learnopengl.com, [Framebuffers](https://learnopengl.com/Advanced-OpenGL/Framebuffers), for the same effects built from OpenGL framebuffer and renderbuffer objects.
- Victor Powell's [Image Kernels explained visually](https://setosa.io/ev/image-kernels/), an interactive page for trying kernels on a photo.

Previous: [15. Blending](../15-Blending/README.md) | Next: [17. Cubemaps](../17-Cubemaps/README.md)
