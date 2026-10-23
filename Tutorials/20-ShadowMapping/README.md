# 20. Shadow Mapping

Every lit scene so far has had a flaw you may not have noticed: nothing casts a
shadow. A cube between the lamp and the floor still lets the floor behind it
light up fully, because the lighting in chapter 09 (Lighting) only asks which
way a surface faces, never whether anything stands between it and the light.
Shadows are one of the strongest cues we have for where things are: without
them, objects float. In this chapter you will add them with **shadow mapping**,
the technique nearly every real-time renderer uses. A sun slowly circles the
sky over a field of boxes, and their shadows swing round across the floor
with it.

Shadow mapping takes two passes. The first renders the scene **from the
light** and keeps only the depth buffer, the **shadow map**. The second renders
the scene from the camera as usual, and each fragment asks the shadow map one
question: is something closer to the light than I am? Along the way you will
meet the **orthographic projection** a directional light needs, the mapping
from the light's clip space into the shadow map (which differs from OpenGL's in
two ways worth knowing), and the three classic artefacts: **shadow acne**,
**peter-panning** and the edge of the light's **frustum**. Finally,
**percentage-closer filtering** softens the hard, blocky shadow edges.

## The idea: what the light can see

A point is in shadow when the light cannot see it. So put a camera where the
light is, looking the way the light shines, and render the scene's depth. Each
texel of that depth buffer then holds the distance from the light to the
**closest** surface along that ray: the surface the light actually hits.

```
   light ---> ===box===                 the shadow map stores, per ray,
     \  \         \                     the depth of the first thing hit
      \  \         \
       \  \         \  (shadowed)
   =====A==\=========B===========  floor
             lit
```

Later, while drawing the floor from the camera, a fragment at point `B` works
out where it is from the light's point of view: which texel of the shadow map
its ray falls in, and how far from the light it is. The texel says the closest
thing on that ray is the box's top, much nearer the light than `B`. So `B` is
in shadow. Point `A` finds its own depth in the texel (it is the first thing on
its ray), so `A` is lit.

That comparison is the whole technique. Everything else in this chapter is
about doing it accurately.

## The light's camera

The light's camera is built from the same two matrices as any camera: a
**view** matrix that puts the light at the origin looking down -z, and a
**projection** matrix that shapes what it sees into clip space.

A point light or a spotlight sends rays out from one point, which a
**perspective** projection models. The sun is so far away that its rays
arrive parallel, and the projection for parallel rays is **orthographic**: no
divide by distance, no foreshortening, just a box.

```
   perspective (a spotlight)          orthographic (the sun)

        light                          +----------------+
         /\                            |  |  |  |  |  | |
        /  \                           |  |  |  |  |  | |   every ray
       /    \                          |  v  v  v  v  v |   parallel
      /      \                         |                |
     /________\                        +----------------+
```

`Mat4::orthographic(left, right, bottom, top, near, far)` builds that box in
the light's view space: x from `left` to `right`, y from `bottom` to `top`, and
z from `near` to `far` units in front of the eye. Everything inside lands in
clip space; everything outside is clipped. We centre a box 18 units across on
the scene and place the light's "eye" 20 units back along the sun's direction:

```cpp
auto lightView =
    Mat4::lookAt(lightDirection * lightDistance, {0, 0, 0}, {0, 1, 0});
auto lightProjection = Mat4::orthographic(-lightHalfExtent,
                                          lightHalfExtent,
                                          -lightHalfExtent,
                                          lightHalfExtent,
                                          lightNear,
                                          lightFar);
auto lightViewProjection = lightProjection * lightView;
```

The box's size is a trade. Every texel of the shadow map covers
`18 / 2048` of a world unit here, about 0.009 units. Make the box bigger and the
shadows get blockier; make it smaller and parts of the scene fall outside it.
Fitting the box tightly to what the camera sees is what large outdoor games
spend a lot of effort on (see "Further reading").

An orthographic projection also keeps depth **linear**: halfway between the
near and far planes is depth 0.5, unlike the perspective depth chapter 08
(Coordinate Systems) produced, which crowds most of its range near the camera.
That makes the depth comparisons equally precise across the whole scene.

## The shadow pass: depth only

Chapter 16 (Framebuffers) rendered a scene into a texture so that a later pass
could sample its **colour**. Here we want a later pass to sample its **depth**,
which takes one more flag on the texture descriptor:

```cpp
auto shadowDescriptor = TextureDescriptor {};
shadowDescriptor.width = shadowMapSize;
shadowDescriptor.height = shadowMapSize;
shadowDescriptor.format = shadowColorFormat;
shadowDescriptor.renderTarget = true;
shadowDescriptor.sampleableDepth = true;
shadowTarget.emplace(Device::shared().makeTexture(shadowDescriptor));
```

`sampleableDepth` gives the target a depth buffer that a later pass may read,
not only test against. (It implies `depth`.) A depth buffer in EACP is a plane
of a render target, not a texture of its own, so the target still has a colour
plane and a colour format. Nothing will be written into it, so we pick the
smallest format there is, `R8Unorm`.

Graphics APIs can run a pass with no fragment shader at all, which is exactly
what a shadow pass wants, and EACP's lower-level API supports it (see
"Depth without a fragment stage" in the GPU README). A `ShaderProgram`, though,
always generates a fragment stage. So the shadow shader writes a constant
colour, and the pipeline throws it away with a colour **write mask** of none:

```cpp
struct ShadowDepthShader final : ShaderProgram
{
    ShadowDepthShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        setPosition(lightModelViewProjection * float4(position, 1.0f));
        setFragment(float4(constant(0.0f), 0.0f, 0.0f, 1.0f));
    }

    Uniform<Float4x4> lightModelViewProjection;

    EACP_SHADER(lightModelViewProjection)
};
```

```cpp
auto shadowPipeline = RenderPipelineDescriptor {};
shadowPipeline.sampleCount = 1;
shadowPipeline.depth = true;
shadowPipeline.cullMode = CullMode::Back;
shadowPipeline.colorFormat = pixelFormatFor(shadowColorFormat);
shadowPipeline.colorWriteMask = ColorWriteMask::none();
```

The pipeline matches its target as in chapter 16: one sample, the target's
colour format, depth on. The vertex stage is everything: the model matrix,
then the light's view and projection, folded into one matrix on the CPU as in
chapter 08.

One more thing is needed when the pass runs. By default a pass **clears** its
depth at the start and **discards** it at the end, because a frame's depth is
normally useless once the frame is drawn. On Apple's GPUs it often never leaves
on-chip tile memory at all. We need ours to survive, so the pass asks to keep
it:

```cpp
{
    auto descriptor = RenderPassDescriptor {};
    descriptor.depthAction = DepthAction::Keep;

    auto pass = frame.beginPass(*shadowTarget, descriptor);
    forEachObject(
        [&](const Mat4& model, Vec3, float)
        {
            depthShader.lightModelViewProjection = lightViewProjection * model;
            pass.draw(depthShader);
        });
}
```

The braces end the pass before the camera's pass begins. As in chapter 16, a
texture cannot be read by the pass still drawing into it.

`forEachObject` walks the floor and the boxes and hands each one's model
matrix, colour and checker flag to a lambda. Both passes use it, so they are
guaranteed to draw the same scene; a box drawn only in the camera pass would
cast no shadow, and one drawn only in the shadow pass would cast a shadow of
nothing.

## From the light's clip space to the shadow map

Now the camera pass. Each vertex goes through the light's matrices as well as
the camera's, and the result travels to the fragment stage as a varying:

```cpp
auto lightClip = varying(lightViewProjection * float4(worldPosition, 1.0f));
```

In the fragment stage that light-space clip position has to become two
things: a **UV** to look up in the shadow map, and a **depth** to compare
against what the lookup returns. This is the one step where EACP and OpenGL
differ, in two ways, and the OpenGL tutorials you may have read get both
wrong for EACP if copied literally.

First divide by `w`, as the GPU does for `setPosition`, to get normalised
device coordinates. For an orthographic projection `w` is 1 and this does
nothing, but a spotlight's perspective projection needs it, so it is a good
habit. Then:

```
                      OpenGL                          EACP (Metal, D3D12)

   NDC x, y           -1..1, y up                     -1..1, y up
   NDC z              -1..1                           0..1
   texture v = 0      bottom row                      top row

   uv                 xy * 0.5 + 0.5                  (x * 0.5 + 0.5,  0.5 - y * 0.5)
   depth              z * 0.5 + 0.5                   z, as it is
```

- **Depth range.** OpenGL's clip space runs z from -1 at the near plane to +1
  at the far plane, and a depth buffer stores 0..1, so OpenGL shadow code
  remaps z with `* 0.5 + 0.5`. EACP's `Mat4` projections, like Metal and
  D3D12, already produce 0..1, which is exactly what the shadow pass wrote.
  Remapping it again would squash every depth into 0.5..1 and compare the
  wrong numbers.
- **UV origin.** As chapter 06 (Textures) showed, EACP's UV (0, 0) is the
  **top-left** texel, while clip-space y points **up**. The shadow pass wrote
  clip-space y = +1 into row 0, the top. So v has to run the opposite way to
  y: `v = 0.5 - y * 0.5`. OpenGL's bottom-left origin lines up with y-up, so
  its code uses `y * 0.5 + 0.5`. Copy that, and every shadow is mirrored front
  to back across the light's view.

In the shader:

```cpp
auto ndc = lightClip.xyz() / lightClip.w();

auto uv = float2(ndc.x() * 0.5f + 0.5f, 0.5f - ndc.y() * 0.5f);
auto depth = ndc.z();
```

## Reading depth in a shader

A depth plane is read through its own kind of uniform, `Uniform<TextureDepth2D>`,
and assigned the **render target** whose depth it reads:

```cpp
Uniform<TextureDepth2D> shadowMap;
...
litShader.shadowMap = *shadowTarget;
```

`sample()` on it returns one `Float`, not a `Float4`: a depth buffer has one
channel. The value is the depth the shadow pass wrote, 0 at the light's near
plane and 1 at its far plane. Like `Uniform<Texture2D>`, the member stores a
pointer, so the target must outlive the shaders, which is why `shadowTarget`
is declared before them. Its sampling defaults to nearest and clamp, which is
what the depth comparisons below want: a blend of two depths is a depth that
no surface has.

The test is then one line. `step(edge, x)` is 1 when `x >= edge` and 0
otherwise, so

```cpp
shadow = step(sample(shadowMap, uv), depth);
```

is 1 when this fragment is at least as far from the light as the closest
surface on its ray. Run it like that, though, and the picture is a mess.

## Shadow acne, and the bias

Run the raw comparison and every lit surface is covered in stripes and rings
of false shadow. This is **shadow acne**.

The shadow map is a grid of texels, and each texel holds **one** depth, taken
at its centre. A surface tilted relative to the light has a range of depths
across that texel. Fragments on the far half of it are a little deeper than
the stored value, so they decide they are behind themselves and go dark:

```
            light
              |  |  |  |
              v  v  v  v                each texel stores the depth at its centre
           .--+--.--+--.--+--.          (+); the surface between centres lies
   surface  \__   \__   \__   \__       partly behind that stored value, and
               \__   \__   \__          that part shadows itself: stripes
```

The fix is to compare against a slightly smaller depth, pretending each
fragment is a little closer to the light than it is. That offset is the
**bias**. How much is enough depends on the slope: a surface facing the light
head on has almost the same depth across a texel, while one the light grazes
has a large spread. So the bias grows as `n . l` falls, the same `n . l` the
diffuse term already computed. That is a **slope-scaled** bias:

```cpp
auto bias = useSlopeScaledBias ? max(0.006f * (1.0f - nDotL), 0.0005f)
                               : constant(0.0f);
auto testDepth = depth - bias;
```

The numbers are in depth units. With the light's 39-unit depth range, 0.0005
is about 0.02 world units and 0.006 about 0.23 units, at the grazing end.
They are tuned to this scene's shadow-map resolution and box size; change
either and they need retuning, which is the honest truth about shadow bias.
Set `useSlopeScaledBias` to `false` at the top of `Main.cpp` to see the acne
for yourself. (Look closely at a box face the sun barely grazes and you may
still find faint stripes: there `n . l` is nearly 0, the bias it would need heads off
to infinity, and a linear bias cannot keep up. The diffuse light on such a face is nearly
zero anyway, so the stripes are faint, which is the usual compromise.)

(`useSlopeScaledBias` is a C++ `constexpr bool`, so the `?:` picks a graph
while it is recorded, as chapter 16's effects did: the compiled shader holds
only the branch you chose.)

## Peter-panning

A bias that is too large has the opposite problem. Every fragment pretends to
be so much nearer the light that the bottom of a shadow, where the caster
meets the floor, no longer counts as hidden. The shadow comes loose from its
object, and the object seems to hover, like Peter Pan detached from his
shadow. Try multiplying the bias by 20 and watch the gaps open at the base of
each box, widest when the sun is low.

So the bias is a balance: big enough to remove acne on the steepest surfaces
you care about, small enough that shadows stay attached. A slope-scaled bias
is how you get both, by spending a large bias only where the slope needs it.
Two other common tools are a higher-resolution or tighter shadow map, which
shrinks the texels and so the bias they need, and rendering **back faces only**
into the shadow map, which moves the stored depth to the far side of each
closed object (one of the exercises).

## Outside the light's box

The shadow map covers only what is inside the light's orthographic box. A
fragment outside it gets a UV outside 0..1 or a depth past 1, and whatever
the sampler returns there (with clamp addressing, the nearest edge texel) has
nothing to do with that fragment. Left alone, that paints large false shadows
across the far parts of the floor.

The floor here is 30 units wide and the light's box only 18, so this really
happens. The shader treats everything outside the box as lit, by multiplying
the shadow by a mask that is 1 only inside:

```cpp
auto inside = step(0.0f, uv.x()) * step(uv.x(), 1.0f) * step(0.0f, uv.y())
              * step(uv.y(), 1.0f) * step(depth, 1.0f);

return shadow * inside;
```

"Lit" is the right guess for an outdoor scene: a sun shines on almost
everything. With the bias turned off you can see where the box ends, because
the acne stops there in a straight line.

## Soft edges: percentage-closer filtering

With acne gone, the shadow edges are still hard and jagged: each shadow-map
texel is either in or out, and up close you can see their outline as stair
steps.

The obvious idea is to blur the shadow map, but averaging **depths** is
meaningless: the average of a box's depth and the floor's is a depth halfway
between, where there is no surface at all. What you can average is the
**results** of the test. Run the comparison against each of the nine texels
around the fragment and take the fraction that say "shadowed". Where all nine
agree you get 0 or 1, as before; along an edge you get the steps in between.
That is **percentage-closer filtering**, PCF:

```cpp
constexpr auto texel = 1.0f / static_cast<float>(shadowMapSize);

for (auto dy = -1; dy <= 1; ++dy)
{
    for (auto dx = -1; dx <= 1; ++dx)
    {
        auto offset = float2(constant(dx * texel), dy * texel);
        auto closest = sample(shadowMap, uv + offset);
        shadow = shadow + step(closest, testDepth);
    }
}

shadow = shadow / 9.0f;
```

The loops are C++, so as in chapter 16's convolution kernels they unroll while
the graph is recorded into nine `sample()` calls. The edge softens over about
three texels, here about 0.03 world units.

GPUs can do one of these comparisons in hardware: a **comparison sampler**
returns the comparison result instead of the depth, and with linear filtering
blends the four nearest results, which is a 2x2 PCF in a single sample.
EACP's lower-level API can bind one (`Device::makeSampler` with a compare
function); the shader EDSL's sampling is just a filter and an address mode,
so this chapter does the comparisons itself, which also makes the technique
visible.

## Walking through the shaded colour

The lit shader is chapter 09's Phong shader, changed from a point light to a
**directional** light: `lightDirection` is the direction towards the sun, the
same for every fragment, instead of being computed from a light position. The
floor gets chapter 16's checkerboard so the shadows have a surface to fall on
that shows perspective.

```cpp
auto nDotL = dot(n, l);
auto ambient = 0.2f;
auto diffuse = max(nDotL, 0.0f);
auto specular = 0.25f * step(0.0f, nDotL) * pow(max(dot(v, r), 0.0f), 32.0f);

auto shadow = shadowAmount(lightClip, nDotL);
auto lightAmount = 1.0f - shadow;

auto lit = (ambient + lightAmount * diffuse) * albedo + lightAmount * specular;
```

The shadow removes only the light that comes **directly** from the sun:
diffuse and specular. Ambient stays, because it stands for light bounced in
from everywhere else, which is why shadows are dim rather than black. The
`step(0.0f, nDotL)` keeps a specular highlight off faces turned away from the
light; chapter 09's single cube never showed that edge case, but here the
light can sit low behind a box.

`shadowAmount` is an ordinary C++ member function taking and returning
handles, as chapter 13 (Light Casters) used for each light type. It holds
everything from the last five sections: the divide, the UV and depth, the
bias, the nine tests and the outside mask.

## The debugging inset

When shadows go wrong, the first thing to look at is the shadow map itself.
The inset in the corner draws it: a quad whose fragment shader samples the
same depth uniform and writes the value as grey, near the light dark and far
light. Because the light's depth is linear, it needs no conversion.

It goes in the same pass as the scene, squeezed into a corner with
`setViewport`, which remaps clip space onto a rectangle in pixels measured
from the top-left:

```cpp
auto size = frame.pixelSize();
auto side = std::min(size.x, size.y) * 0.3f;
auto margin = 12.0f * frame.backingScale();

pass.setViewport({size.x - side - margin, size.y - side - margin, side, side});
pass.draw(insetShader);
pass.clearViewport();
```

The window's pass has a depth buffer, so the inset's pipeline still declares
`depth = true`, but it compares with `Always` and writes nothing, so the
scene cannot hide it. Click the window to hide or show it; the constructor calls
`setHandlesMouseEvents(true)` so that the view receives the click at all.

In the inset you can see the boxes as darker shapes (their tops are nearer the
light), the floor as a gradient (its far side is further from a light that
looks down at it at an angle), and white corners where the light's box
reaches past the edge of the floor and the depth stays at its cleared value of
1, the far plane.

## What you should see

A field of seven boxes of different heights on a pale checkered floor under a
blue sky. The sun circles slowly, about 40 degrees above the horizon, and the
boxes' shadows swing round with it, stretching across the floor and climbing
the sides of neighbouring boxes when one stands in another's way. The tall
orange box's shadow falls over its neighbours at some angles. Shadow edges are
crisp but not jagged, with no stripes on any lit face. In the bottom-right
corner, the shadow map turns as the light does.

## Try it yourself

1. Set `useSlopeScaledBias` to `false` to see shadow acne, then
   `usePercentageCloserFiltering` to `false` and look closely at a shadow
   edge. *Hint: without PCF each texel's edge shows as a stair step about 0.009 units
   across; make the box bigger (`lightHalfExtent = 25`) to make them obvious.*
2. Multiply the bias by 20 and look at the bases of the boxes. *Hint: that is
   peter-panning; it is worst when the sun is low, because then `n . l` on
   the floor is small and the slope-scaled part of the bias is large.*
3. Render back faces only into the shadow map: set the shadow pipeline's
   `cullMode` to `CullMode::Front`, then try turning the bias off. *Hint: the
   lit faces now compare against the far side of each box, so they stop
   shadowing themselves; acne moves to faces that are turned away from the
   light, where diffuse is already 0. The floor is a thin slab, so it still
   needs a little bias.*
4. Move a box outside the light's box, to the far corner of the floor at
   `x = -13, z = -13`. It casts no shadow.
   Now grow `lightHalfExtent` until it does, and watch the shadow edges get
   blockier. *Hint: the shadow map is the same 2048 texels across whatever
   area it covers.*
5. Widen PCF to a 5x5 kernel. *Hint: change the loops to run from -2 to 2 and
   divide by 25; the edge gets softer and the shader does 25 lookups per
   fragment instead of 9.*
6. Turn the sun into a spotlight: a `Mat4::perspective` light projection
   with a 60 degree field of view from a light position above the scene.
   *Hint: the divide by `w` now matters, and the depth is no longer linear, so
   the bias and the inset need rethinking.*

## Further reading

- EACP GPU README, [Depth](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#depth): `sampleableDepth`, `DepthAction::Keep`, and binding a target's depth plane in a later pass.
- EACP GPU README, [Depth without a fragment stage](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#depth-without-a-fragment-stage): a true vertex-only shadow pipeline, built from a hand-written shader library.
- EACP GPU README, [Sampling](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#sampling): per-bind samplers, including a comparison sampler for hardware PCF.
- EACP GPU README, [Viewport, and how it differs from a scissor](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#viewport-and-how-it-differs-from-a-scissor).
- `Apps/GPU/DepthSampling` in EACP: one pass writes depth, a later one samples it through `Uniform<TextureDepth2D>`.
- learnopengl.com, [Shadow Mapping](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping), the same technique in OpenGL (remember the two differences above), and [Cascaded Shadow Mapping](https://learnopengl.com/Guest-Articles/2021/CSM) for fitting shadow maps to large outdoor scenes.
- Microsoft, [Common Techniques to Improve Shadow Depth Maps](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps): bias, fitting the light frustum, and PCF in depth.

Previous: [19. Anti-Aliasing](../19-AntiAliasing/README.md) | Next: [21. Normal Mapping](../21-NormalMapping/README.md)
