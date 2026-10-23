# 15. Blending

Everything drawn so far has been opaque: each fragment replaced whatever was
in the colour buffer before it. Plenty of things in the world are not like
that. A leaf has a shape that is not a rectangle, and a pane of coloured glass
lets you see what is behind it, tinted. In this chapter you will build a small
scene with both: tufts of **grass** drawn on flat quads whose background is
cut away with **discard**, and coloured **glass windows** mixed into the
picture with **alpha blending**. Along the way you will meet the **blend
equation** and its **factors**, see why transparent objects must not write
depth the way opaque ones do, and **sort** the windows back to front on the CPU
every frame, because the camera orbits the scene and the right order keeps
changing.

## Alpha: the fourth channel

Since chapter 02 every `setFragment` colour, and since chapter 06 (Textures)
every texel, has had four components, and the fourth, **alpha**, has always been 1. Alpha is
usually read as *opacity*: 1 is solid, 0 is fully transparent, 0.35 lets 65%
of what is behind show through. Nothing on the GPU makes alpha mean that by
itself, though. It is just a number carried along with red, green and blue,
until something chooses to read it. This chapter has two such somethings.

## Fully transparent texels: discard

Here is the grass texture, in a 128 x 128 image. Inside a blade the alpha is
1; everywhere else it is 0.

```
   alpha of the grass texture       drawn on a quad, opaquely
   (# = 1, . = 0)

   . . . . . # . . . . . .          +-----------------------+
   . . . # . # . . # . . .          |     /\  ||   /\       |  <- the "transparent"
   . . . # . # # . # . . .          |    /  \ || /  \       |     texels still draw:
   . . # # . # # . # # . .          |   whatever colour     |     black, and they
   . # # # # # # # # # # .          |   the texture holds   |     write depth too
   . # # # # # # # # # # .          +-----------------------+
```

Draw that on a quad the ordinary way and the alpha is ignored: you get the
blades on a rectangle of whatever RGB the empty texels happen to hold. What we
want is for those texels not to be drawn **at all**, as if the quad had the
shape of the grass.

That is what **discarding** a fragment does. The fragment shader decides that
this fragment does not exist, and the GPU drops it before anything is written:
no colour, and, just as important, no depth. The EDSL spells it as a test on a
value:

```cpp
setDiscardBelow(alpha, 0.5f);
```

Any fragment whose `alpha` is below 0.5 is thrown away. This is often called
the **alpha test** and the threshold the **alpha cutoff**. It is one call
recorded into the shader graph rather than an `if` with a `discard` inside it,
because there is only one question worth asking here: is this value under a
threshold?

The fragments that survive are drawn exactly like opaque ones. They write
depth, they need no particular draw order, and the depth buffer from chapter
08 (Coordinate Systems) sorts them out against everything else. That makes
discard the right tool whenever every texel is either fully there or fully
not: foliage, chain-link fences, the holes in a grate, a sprite's cut-out.

### The edge of the texture

Linear filtering (chapter 06) blends the four texels nearest the sample point.
At the very top of the quad, with the `Repeat` address mode, two of those four
come from the **bottom** row of the image, where the blades are solid. The
filtered alpha there can climb over 0.5 and a thin line of grass appears
floating above every tuft. Using `Clamp` for a cut-out texture stops the
neighbours wrapping round. Our grass shader does.

## Partly transparent: blending

Glass is not all-or-nothing. A window texel with alpha 0.35 should show 35% of
its own colour and 65% of what is behind it. Discard cannot do that: a fragment
either replaces the pixel or vanishes. What we need is to **mix** the new
fragment with the colour already in the colour buffer. That mixing is
**blending**, and it is not done by your shader but by fixed-function hardware
after it, configured in the pipeline.

The hardware evaluates one equation per pixel:

```
   result = source * sourceFactor  (op)  destination * destinationFactor

   source       the colour the fragment shader just produced
   destination  the colour already in the attachment at this pixel
   factors      chosen from a fixed list, below
   (op)         Add, Subtract, ReverseSubtract, Min or Max
```

The factors come from a short list: `Zero`, `One`, `SourceColor`,
`SourceAlpha`, `DestinationColor`, `DestinationAlpha`, and a `OneMinus...`
version of each, plus one special case. For see-through surfaces you want the
classic **"over"** blend: weight the new colour by its own alpha, and what is
behind by the rest.

```
   result = source * source.a  +  destination * (1 - source.a)

   sourceFactor      = SourceAlpha
   destinationFactor = OneMinusSourceAlpha
   op                = Add
```

For example, red glass (1, 0.3, 0.3) at alpha 0.35 over a grey floor pixel
(0.5, 0.5, 0.5):

```
   r = 1.0 * 0.35 + 0.5 * 0.65 = 0.675
   g = 0.3 * 0.35 + 0.5 * 0.65 = 0.43
   b = 0.3 * 0.35 + 0.5 * 0.65 = 0.43        a reddish grey: the floor, tinted
```

The alpha channel itself has its own pair of factors and its own operation.
On the window's drawable nobody reads the resulting alpha, so it hardly
matters; when you blend into a texture you will read back later it does, and
`One, OneMinusSourceAlpha` is the choice that keeps that texture's alpha
meaningful.

### Blending in EACP

EACP names the blends that keep coming up in a `BlendMode`: `None` (the
default), `AlphaBlend`, which is exactly the "over" blend above,
`AlphaBlendOntoTransparent` and `Additive`. Setting
`pipeline.blendMode = BlendMode::AlphaBlend` would have been enough for this
chapter. To see the machinery, we write the equation out instead, as a
`BlendState`, and put it in the descriptor's `blend` field. When `blend` is
set it wins over `blendMode`:

```cpp
auto blend = BlendState {};
blend.enabled = true;
blend.sourceColor = BlendFactor::SourceAlpha;
blend.destinationColor = BlendFactor::OneMinusSourceAlpha;
blend.colorOperation = BlendOperation::Add;
```

`blendStateFor(mode)` returns any of the named modes written out the same way,
which is a handy way to check what a preset actually does.

## The order problem

Blending reads the destination, the picture so far. That makes it depend on
what has **already been drawn**, which the depth buffer let us forget about
for opaque objects. Two rules follow.

**Opaque first, transparent after.** A window blends with what is behind it
only if that is already in the colour buffer. Draw a window and then the crate
behind it, and the crate (opaque, nearer than the far plane) simply paints over
the window's pixels; the blend happened against the sky colour and is now
lost.

**Transparent objects back to front.** The same is true between windows. Two
windows overlap on screen, the green one behind the purple one:

```
  drawn far to near                     drawn near to far
  (green, then purple)                  (purple, then green)

  green blends over the floor;          purple blends over the floor;
  purple blends over that:              green blends over that and ends up
  you see the floor through both        looking *in front* of purple
  panes, which is right.                which is wrong.
```

### And depth writes make it worse

There is a second, sneakier failure. A window, like any surface, writes its
depth by default. Draw the near purple window first and it records its depth
at every pixel it covers, the glass parts included. When the green window
behind it arrives, its fragments are further away than what the depth buffer
holds, so the depth test throws them away. The green window does not just
blend in the wrong order; behind the purple glass it is **gone**, with a sharp
edge where purple's quad ends.

So the second piece of pipeline state for transparent geometry is
`depthWrite = false`, which chapter 08 promised. The depth **test** stays on,
so a crate in front of a window still hides it. Only the **write** goes:
windows never hide each other, they only blend.

<!-- snippet: illustrative -->
```cpp
auto glass = foliage;
glass.depthWrite = false;      // test against the opaque depth, never write it
glass.blend = overBlend();
```

Turning depth writes off removes the disappearing, but not the wrong order:
blending is still order-dependent. Only sorting fixes that.

### Sorting on the CPU

So each frame, after the opaque objects, we sort the windows by their distance
from the camera, furthest first, and draw them in that order. The camera moves,
so the order is recomputed every frame. Five windows sort in no time; a scene
with thousands of particles is where people start sorting on the GPU instead.

Sorting whole objects by their centres is an approximation. It is exact for
flat, non-intersecting quads like ours most of the time, but two long panes
can be ordered one way by their centres and overlap the other way at some
pixel, and two surfaces that cross each other cannot be ordered at all. The
real fixes, **order-independent transparency** (depth peeling, weighted
blended OIT), are a topic of their own. Centre-sorting plus no depth writes is
what most real-time renderers ship with.

## Walking through the code

Two switches at the top let you see both problems. With their default values
the scene is right; flip them and run again.

```cpp
constexpr bool sortWindows = true;
constexpr bool windowsWriteDepth = false;
```

The vertex carries a position, a normal (for a little shading) and a UV:

```cpp
struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};
```

There are three meshes, all built from one helper, `addQuad`, that appends
four corners and two counter-clockwise triangles. The cube is chapter 09's,
with UVs added; the floor is one big quad with UVs running to 5 so a
repeating sampler tiles it; and both grass and windows are the same upright
1 x 1 quad, standing on y = 0 and facing +z, placed in the world by their
model matrices:

```cpp
Mesh makeUprightQuad()
{
    auto mesh = Mesh {};
    addQuad(mesh,
            {Vec3 {-0.5f, 0, 0},
             Vec3 {0.5f, 0, 0},
             Vec3 {0.5f, 1, 0},
             Vec3 {-0.5f, 1, 0}},
            {0, 0, 1},
            {1, 1});
    return mesh;
}
```

The four textures are all made by `makeTexture`, which asks a function for the
RGBA of each texel and uploads the result, just like the checkerboard in
chapter 06. The two that matter here are the grass, alpha 1 inside a blade and
0 outside:

```cpp
                if (std::abs(u - centre) < halfWidth)
                    return Vec4 {0.20f + 0.25f * t, 0.45f + 0.35f * t, 0.12f, 1};
            }

            return Vec4 {0, 0, 0, 0};
```

and the window, an opaque frame and cross bars around near-white glass at
alpha 0.35, with a slightly more opaque diagonal streak:

```cpp
                           if (edge < 0.07f || bar < 0.025f)
                               return Vec4 {0.85f, 0.85f, 0.85f, 1};

                           auto streak =
                               std::abs(u + v - 0.8f) < 0.08f ? 0.25f : 0.0f;
                           return Vec4 {1, 1, 1, 0.35f + streak};
```

The texture is near white so that one texture serves every window: each
multiplies it by its own **tint**.

### One shader, two flags

Every object uses the same `SceneShader`. It samples its texture, multiplies by
a tint, and applies a fixed directional light so the crate faces are told
apart. The light uses `abs(dot(n, l))` rather than chapter 09's
`max(dot(n, l), 0)`, because a grass quad or a window seen from behind should
not turn dark. (The normals go through `model` directly, which is safe here
because every model matrix is a rotation, a translation and a uniform scale;
see chapter 09.)

```cpp
struct SceneShader final : ShaderProgram
{
    SceneShader(TextureSampling sampling, bool alphaTest)
        : alphaTested(alphaTest)
    {
        image.sampling = sampling;
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);
        auto uv = varying(vertexInput(&Vertex::uv));

        auto worldNormal = (model * float4(normal, 0.0f)).xyz();
        setPosition(viewProjection * model * float4(position, 1.0f));

        auto n = normalize(varying(worldNormal));
        auto texel = sample(image, uv);

        // abs() so that a quad seen from behind is lit the same as from in
        // front: grass and glass have no "wrong" side.
        auto shade = 0.55f + 0.45f * abs(dot(n, lightDirection));
        auto color = texel.xyz() * tint.xyz() * shade;
        auto alpha = texel.w() * tint.w();

        // The alpha test: a fragment whose alpha is under one half never
        // reaches the colour or depth buffer at all.
        if (alphaTested)
            setDiscardBelow(alpha, 0.5f);

        setFragment(float4(color, alpha));
    }

    bool alphaTested;

    Uniform<Float4x4> model;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> lightDirection;
    Uniform<Float4> tint;
    Uniform<Texture2D> image;

    EACP_SHADER(model, viewProjection, lightDirection, tint, image)
};
```

Look at the `if (alphaTested)`. It is a plain C++ `if`, run once while
`define()` records the graph, not per pixel on the GPU. A shader built with
`alphaTest = true` contains the discard; one built with `false` does not
contain it at all. That is the same trick as the sampler state in chapter 06:
anything decided before `compile()` is baked into that program. The windows
must **not** have the alpha test: their glass, at 0.35, would be discarded
and only the frames would remain.

`setFragment` outputs the alpha as well as the colour. For the opaque objects
it is 1 and nobody reads it; for the windows it is what the blend stage
multiplies by.

### Three pipelines

The constructor builds three descriptors, each a small change to the last:

```cpp
auto opaque = RenderPipelineDescriptor {};
opaque.sampleCount = sampleCount();
opaque.depth = true;
opaque.cullMode = CullMode::Back;

// Grass is a flat quad seen from both sides, so nothing is culled.
// Otherwise it is opaque: discarded texels are simply not there.
auto foliage = opaque;
foliage.cullMode = CullMode::None;

// Glass is tested against the depth the opaque objects wrote, so a
// crate in front still hides it, and it blends instead of replacing.
auto glass = foliage;
glass.depthWrite = windowsWriteDepth;
glass.blend = overBlend();
```

The floor and the crates are ordinary opaque, culled geometry. The grass needs
both faces, since the camera goes round it, but nothing else: the discard is in
its shader, not its pipeline. The glass adds the two things this chapter is
about, no depth writes and the "over" blend, written out in `overBlend()`:

```cpp
BlendState overBlend()
{
    auto blend = BlendState {};
    blend.enabled = true;
    blend.sourceColor = BlendFactor::SourceAlpha;
    blend.destinationColor = BlendFactor::OneMinusSourceAlpha;
    blend.colorOperation = BlendOperation::Add;
    blend.sourceAlpha = BlendFactor::One;
    blend.destinationAlpha = BlendFactor::OneMinusSourceAlpha;
    blend.alphaOperation = BlendOperation::Add;
    return blend;
}
```

Each shader instance is then given its mesh, pipeline and texture by a small
`prepare` helper. The four shaders are declared with their samplers; only the
floor repeats, and the grass clamps for the reason in "The edge of the
texture":

```cpp
SceneShader floorShader {{TextureFilter::Linear, TextureAddressMode::Repeat},
                         false};
SceneShader crateShader {{TextureFilter::Linear, TextureAddressMode::Clamp},
                         false};
SceneShader grassShader {{TextureFilter::Linear, TextureAddressMode::Clamp},
                         true};
SceneShader windowShader {{TextureFilter::Linear, TextureAddressMode::Clamp},
                          false};
```

### Drawing one program many times

As in chapters 11, 13 and 14, one shader instance serves many objects: here
two crates, twelve grass quads and five windows share four. That works
because `pass.draw` copies the program's uniforms into the command stream
**at the moment of the call**: change `model` and draw again, and the second
draw gets the new matrix while the first keeps the old one.

```cpp
for (auto position: cratePositions)
{
    crateShader.model = Mat4::translation(position);
    pass.draw(crateShader);
}
```

The grass loop does the same, drawing each tuft as two quads crossed at right
angles, so that it never shrinks to a line when the camera sees it edge-on.

### Sorting the windows

`render` starts by moving the camera round a circle of radius 5.5, a little
above the scene, and setting the shared uniforms. Then come the opaque draws
(floor, crates, grass) in no particular order, and finally the windows.

```cpp
auto order = std::array<int, std::size(windows)> {};

for (auto i = 0; i < (int) order.size(); ++i)
    order[i] = i;

if (sortWindows)
{
    auto distanceTo = [&](int i)
    {
        auto centre = windows[i].position + Vec3 {0, 0.5f, 0};
        return lengthSquared(centre - cameraPosition);
    };

    std::sort(order.begin(),
              order.end(),
              [&](int a, int b) { return distanceTo(a) > distanceTo(b); });
}

for (auto i: order)
{
    windowShader.model = Mat4::translation(windows[i].position)
                         * Mat4::rotationY(windows[i].yaw);
    windowShader.tint = windows[i].tint;
    pass.draw(windowShader);
}
```

We sort a list of **indices** rather than the windows themselves, so the
scene description stays a constant table. The key is the squared distance
from the camera to each window's centre (half a unit above its stored bottom
edge). Squared, because comparing squares orders the same way as comparing
distances and skips a square root. `>` puts the furthest first.

The `windows` table is listed nearest first as seen from where the camera
starts, the worst possible order, so that turning `sortWindows` off shows the
problem straight away.

## What you should see

A checkered floor under a pale sky, two wooden crates, six tufts of grass and
five coloured windows (purple, red, green, blue and amber) standing among
them, with the camera slowly circling. Look through the purple window at the
green one behind it: you see the floor through both, tinted by both. The
grass blades have clean outlines and no rectangle around them, and they hide
and are hidden by the crates correctly.

Now flip the switches:

- `sortWindows = false`: at the start, the green and blue frames appear to be
  *in front* of the purple and red windows that are actually nearer. As the
  camera comes round the far side, the table order happens to become right and
  the problem goes away, then comes back.
- `windowsWriteDepth = true` as well: the green window's lower half vanishes
  behind the purple glass, cut off along purple's edge, and the left of the blue
  window disappears behind the red one.
- `windowsWriteDepth = true` with `sortWindows = true`: correct again, for these
  quads. Drawn far to near, no window is ever behind one already drawn. It
  still fails as soon as two transparent surfaces cross, which is why
  transparent pipelines normally leave depth writes off anyway.

## Try it yourself

1. Give the grass shader `alphaTest = false`. *Hint: the "transparent" texels
   now draw as black rectangles, and worse, they write depth, so the grass
   quads cut holes in each other and in whatever is drawn after them.*
2. Change the grass sampler to `TextureAddressMode::Repeat` and look closely at
   the top of each tuft. Then try `TextureFilter::Nearest`. *Hint: the stray
   line comes from filtering across the wrap; with nearest filtering there is
   no blending of neighbours, so it goes away, at the cost of blocky edges.*
3. Change the alpha cutoff from 0.5 to 0.1 and to 0.9, with the grass sampler
   linear. *Hint: the cutoff decides where along the filtered edge the blade
   ends, so it makes the blades fatter or thinner.*
4. Replace `overBlend()` with additive blending (`destinationColor =
   BlendFactor::One`) and turn sorting off. *Hint: addition does not care about
   order, so sorting no longer matters, but nothing looks like glass: it looks
   like light. This is why fire and sparks are usually additive.*
5. Make the windows multiply what is behind them, like real stained glass:
   `sourceColor = BlendFactor::DestinationColor`, `destinationColor =
   BlendFactor::Zero`. *Hint: the result is destination * source, so white glass
   leaves the scene unchanged and the tint filters it; the window's alpha no
   longer plays any part.*
6. Move the green window so it crosses the purple one
   (try `{0.9f, 0.1f, 1.7f}` with a yaw of `-0.5f`). *Hint: no order of two
   whole objects is right at every pixel when they intersect; this is the limit
   of sorting.*

## Further reading

- EACP GPU README, [Blending past the four named modes](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#blending-past-the-four-named-modes): `BlendState`, `blendStateFor` and `colorWriteMask`.
- EACP GPU README, [Pipeline state](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#pipeline-state): why translucent geometry tests depth but does not write it.
- `BlendFactor` and `BlendOperation` in EACP's `Lib/eacp/GPU/Pipeline/RenderPipeline.h`, with a note on every value, and `setDiscardBelow` in `Lib/eacp/GPU/Codegen/ShaderProgram.h`.
- `Apps/GPU/Blending` in EACP: the same three overlapping circles drawn with `None`, `AlphaBlend` and `Additive`, side by side.
- learnopengl.com, [Blending](https://learnopengl.com/Advanced-OpenGL/Blending), and its [OIT](https://learnopengl.com/Guest-Articles/2020/OIT/Introduction) guest articles for what lies beyond sorting.

Previous: [14. Stencil Testing](../14-StencilTesting/README.md) | Next: [16. Framebuffers](../16-Framebuffers/README.md)
