# 13. Light Casters

So far every scene has had one light, a point hanging somewhere near the
object. Real scenes have several, and they are not all the same kind: the sun
lights everything from one direction, a bulb lights the area around it and
fades with distance, and a torch or a stage light throws a cone. In this
chapter you will write all three **light casters** (a **directional light**, a
**point light** with **attenuation**, and a **spotlight** with a soft edge) as
small helper functions, and then light a night-time field of cubes with one
directional light, four coloured point lights and a spotlight that sweeps
across the ground, all summed in one fragment shader. Along the way you will
see how the shader EDSL handles helper functions and loops (they are plain
C++), and how a fragment shader reads a list of lights out of a **storage
buffer**.

## One shading function, three ways of arriving

Look back at the Phong formula from chapter 09 (Lighting). Diffuse and
specular need just two things from the light: the unit direction `l` from the
surface towards it, and how much light arrives. Everything that makes one kind
of light different from another is in how those two are worked out. So the
shader has one function that does the Phong arithmetic, and one small function
per light type that computes `l` and the arriving light and hands them over:

```
   directionalLight ---\
   pointLight ---------+--> phong(surface, l, arriving)  -->  this light's colour
   spotLight ---------/

   final colour = ambient + sum of every light's colour
```

Light adds up. Two lamps shining on the same wall make it brighter than either
alone, so a scene with many lights is simply the sum of each light's
contribution, computed separately.

## Directional lights

The sun is so far away that by the time its light reaches us the rays are, for
all practical purposes, parallel. Every point in the scene sees it in the same
direction, at the same strength:

```
     \   \   \   \   \       one direction for every point,
      \   \   \   \   \      no position, no fall-off
       \   \   \   \   \
   ======================
```

A directional light is therefore just a direction (the way the light
*travels*) and a colour. `l` is that direction reversed, the same at every
pixel, and the arriving light is the full colour. In our scene it is a dim
blue moonlight, so that the faces turned towards it are faintly visible and
the coloured lights have darkness to show off against.

## Point lights and attenuation

A point light is chapter 09's light: a position, from which light spreads in
every direction. `l` is the normalised vector from the surface to the
position, different at every pixel. What chapter 09 left out is that real
light **fades with distance**. The same light spread over a sphere twice as
large covers four times the area, so physically the intensity falls as
`1 / d²`. That law is correct but awkward to use: it is infinite at the light
and drops so steeply that a lamp either blows out what is near it or barely
lights anything.

Real-time renderers use a softer, tunable curve instead:

```
                              1
   attenuation = -------------------------------
                 constant + linear*d + quadratic*d²
```

- **constant** is usually 1, which keeps the result from ever going above 1
  (at the light, `d = 0`, so attenuation is exactly 1).
- **linear** makes the light fade steadily as it goes.
- **quadratic** takes over at larger distances and pulls the tail down
  quickly, like the physical law.

```
  attenuation
   1 |*
     | *
     |  *.
     |    `*.
     |       `**..
     |            ``***.....
   0 +-------------------------****---> distance
```

Picking the three numbers is a matter of taste and scale. A useful habit is
to think of the distance at which a light should have faded to almost nothing,
and pick from a table like this one (constant 1 in every row):

| reaches about | linear | quadratic |
|---------------|--------|-----------|
| 7             | 0.7    | 1.8       |
| 13            | 0.35   | 0.44      |
| 20            | 0.22   | 0.20      |
| 50            | 0.09   | 0.032     |
| 100           | 0.045  | 0.0075    |

Our point lights use the 20 row: the scene is about 18 units across, so each
light pools brightly on the ground around it and leaves a faint tint on the
far side.

The attenuation simply scales the light's colour before it reaches `phong`.
It also needs the distance `d`, which falls out of computing `l`: the vector
to the light, divided by its own length, is `l`, and that length is `d`.

## Spotlights

A spotlight is a point light with a lampshade: a position, a direction it
points in, and a **cutoff angle** outside which it gives no light at all.
Whether a surface point is inside the cone depends on the angle `θ` between
the spot's axis and the ray from the spot to the point:

```
                  spot
                   *
                  /|\
                 / | \
                /  |θ \  <- ray to the point
               /   |   \
              / axis    \
             /     |     \
   =========/======|======\=========
            ^ outer cutoff ^
```

Inside means `θ` is smaller than the cutoff. Comparing angles would need an
`acos` per pixel, but there is no need: the dot product of two unit vectors is
already the *cosine* of the angle between them, and cosine shrinks as the
angle grows. So we keep the cutoff as a cosine too, computed once on the CPU,
and compare cosines: the point is inside when `cos θ` is *larger* than
`cos(cutoff)`.

A single cutoff gives a cone with a razor-sharp rim, which no real lamp has.
Real spotlights fade out over a band, so we use **two** cutoffs: an inner
angle inside which the light is at full strength, and an outer angle beyond
which it is off. Between them the strength ramps from 1 to 0:

```
                    cos θ - cos(outer)
   intensity = clamp( ---------------------, 0, 1 )
                    cos(inner) - cos(outer)
```

At the inner angle the fraction is 1, at the outer angle it is 0, and outside
that band the clamp pins it to the ends. Our spot uses 14 and 20 degrees,
which gives the pool of light a soft rim about a sixth of its radius wide.
A spotlight is still a light at a position, so it is attenuated too, with a
very long reach so it does not fade much over the scene.

## Helper functions in the EDSL

Chapter 09 wrote the whole shader inline in `define()`. Here the shader is
split into functions, and nothing special is needed for that. Remember what
`define()` does: it runs **once**, in C++, recording a graph of operations
(chapter 02, Hello Triangle). A `Float3` is a handle to a node in that graph,
so a plain C++ function that takes handles and returns a handle just records
more nodes:

```cpp
Float3 directionalLight(const Surface& surface,
                        const Float3& direction,
                        const Float3& color)
{
    return phong(surface, normalize(-direction), color);
}
```

There is no function call in the generated shader; the function's operations
are inlined where it was called, exactly as if you had typed them out in
`define()`. That is the right result for a GPU anyway, where shader compilers
inline nearly everything. The same goes for the intrinsics: `normalize`,
`dot`, `max` are free functions taking handles, so a helper outside the
shader class can call them as freely as `define()` does.

The same reasoning gives you **structs** for free. The EDSL has no aggregate
type of its own, and needs none: a C++ struct of handles is a perfectly good
way to pass a bundle of values around while recording.

And a C++ `for` loop inside `define()` is an **unrolled** loop in the shader.
It runs at record time, so a loop over four lights leaves four copies of the
point-light arithmetic in the graph. With a count known when the shader is
built, that is what you want: it is what a shader compiler would do with a
fixed-count loop anyway. (When the count must change at run time, the EDSL
has a real `loop` statement too; it is the subject of an exercise.)

## Where the point lights live: a storage buffer

Four point lights, each with a position, a colour and three attenuation terms,
is a small **array of records**. There are three ways to get one into a
shader:

1. **One uniform per field per light**: `pointPosition0`, `pointColor0`, ...,
   `pointPosition3`. It works, but twelve near-identical uniform members and an
   `EACP_SHADER` list to match is clumsy, and there is no way to loop over
   them, since a C++ loop cannot pick a member by number.
2. **A constant array** built inside `define()` with `array(a, b, c, d)` from
   handles (which may be uniforms) and subscripted with `[i]`. It suits a small
   lookup table, but the elements still have to come from somewhere, so it
   does not remove the twelve uniforms.
3. **A storage buffer**: put the records in a `GPU::Buffer` and declare a
   `Uniform<InputBuffer>`, which the fragment stage reads by index. This is
   how a renderer with many lights does it, and it is what we use.

The third is the same buffer read you used in a compute kernel in chapter 10
(Compute); a `ShaderProgram` can read one from either stage. The buffer is a
flat array of floats, and `read3(i)` reads floats `3i`, `3i + 1` and `3i + 2`
as a `Float3`. So if each light is three `Vec3`s back to back, light `n`'s
fields are `read3(3n)`, `read3(3n + 1)` and `read3(3n + 2)`, with no padding
to worry about:

```
   float index:  0  1  2 | 3  4  5 | 6  7  8 | 9 10 11 | 12 ...
   read3 index:  0       | 1       | 2       | 3       | 4 ...
                 position  colour    atten.    position  ...
                 \________ light 0 _________/  \__ light 1 ...
```

Uniforms are still the right home for things that are one of a kind, like the
single directional light and the single spotlight, so those stay ordinary
`Uniform<Float3>` members.

## Walking through the code

The CPU-side record matches the layout above. A `static_assert` guards it: if
someone ever changed a field to a `Vec4`, the shader's indices would silently
read the wrong floats, and the assert stops that at compile time instead.

```cpp
// One point light as the GPU reads it: nine floats, three Vec3s back to back.
// The fragment shader reads the buffer as a flat array of floats, so the record
// needs no padding; record i starts at Vec3 number 3 * i.
struct PointLight
{
    Vec3 position;
    Vec3 color;
    Vec3 attenuation; // constant, linear, quadratic
};

static_assert(sizeof(PointLight) == 9 * sizeof(float));
```

The cube mesh is chapter 09's, unchanged. Then the lighting helpers. First the
bundle of facts about the pixel being shaded, which every helper needs:

```cpp
struct Surface
{
    Float3 position;
    Float3 normal;
    Float3 toEye;
    Float3 albedo;
};
```

(**Albedo** is the usual name for a surface's own colour, the fraction of
each colour of light it reflects diffusely.) Next the shared Phong function.
It is chapter 09's diffuse and specular terms, with `arriving` in place of the
light's colour, so a caller can dim it first:

```cpp
Float3 phong(const Surface& surface, const Float3& toLight, const Float3& arriving)
{
    auto diffuse = max(dot(surface.normal, toLight), 0.0f);
    auto r = reflect(-toLight, surface.normal);
    auto specular =
        specularStrength * pow(max(dot(surface.toEye, r), 0.0f), shininess);

    return (diffuse * surface.albedo + specular) * arriving;
}
```

Attenuation is the formula, with the three coefficients carried in one
`Float3` and taken apart with the `.x()`, `.y()`, `.z()` swizzles:

```cpp
Float attenuation(const Float& distance, const Float3& coefficients)
{
    return 1.0f
           / (coefficients.x() + coefficients.y() * distance
              + coefficients.z() * distance * distance);
}
```

Then the three light casters. The directional light you saw above. The point
light computes the vector to the light once and gets both `l` and `d` from it:

```cpp
Float3 pointLight(const Surface& surface,
                  const Float3& position,
                  const Float3& color,
                  const Float3& coefficients)
{
    auto toLight = position - surface.position;
    auto distance = length(toLight);

    return phong(
        surface, toLight / distance, color * attenuation(distance, coefficients));
}
```

The spotlight is a point light with the cone factor multiplied in. `-l` points
from the spot towards the surface, so its dot product with the spot's axis is
`cos θ`. The two cutoff cosines arrive packed in a `Float2`, inner first:

```cpp
Float3 spotLight(const Surface& surface,
                 const Float3& position,
                 const Float3& direction,
                 const Float3& color,
                 const Float3& coefficients,
                 const Float2& cutoffs)
{
    auto toLight = position - surface.position;
    auto distance = length(toLight);
    auto l = toLight / distance;

    // The cosine of the angle between the spot's axis and the ray from the
    // spot to this point. Larger means closer to the middle of the cone.
    auto theta = dot(-l, normalize(direction));
    auto cone =
        clamp((theta - cutoffs.y()) / (cutoffs.x() - cutoffs.y()), 0.0f, 1.0f);

    return phong(surface, l, color * (cone * attenuation(distance, coefficients)));
}
```

With the helpers written, the shader's `define()` reads like the diagram at
the top of the chapter. The vertex half is chapter 09's. The fragment half
fills in a `Surface`, starts from a small ambient term, and adds each light in
turn. `color` is a C++ variable holding a handle; `color = color + ...`
rebinds it to the new sum node, which is all an accumulation needs at record
time.

```cpp
        auto surface = Surface {
            fragmentPosition,
            normalize(varying(worldNormal)),
            normalize(cameraPosition - fragmentPosition),
            objectColor,
        };

        // A small constant stands in for all the light bouncing around the
        // scene, so nothing is ever completely black.
        auto color = 0.04f * surface.albedo;

        color = color + directionalLight(surface, sunDirection, sunColor);

        // An ordinary C++ loop. It runs once, while define() records the
        // graph, and leaves four calls to pointLight() behind, each reading its
        // own record from the buffer at a fixed index.
        for (auto i = 0u; i < pointLightCount; ++i)
        {
            color = color
                    + pointLight(surface,
                                 pointLights.read3(3 * i),
                                 pointLights.read3(3 * i + 1),
                                 pointLights.read3(3 * i + 2));
        }

        color = color
                + spotLight(surface,
                            spotPosition,
                            spotDirection,
                            spotColor,
                            spotAttenuation,
                            spotCutoffs);

        setFragment(float4(color, 1.0f));
```

`pointLightCount` is one C++ constant that both the shader's loop and the CPU
array below are sized by, so the two cannot disagree. The index `3 * i` is an
unsigned C++ number, so each `read3` gets a literal index baked into the
shader.

In this chapter the ambient term is one constant for the whole scene rather
than a share of each light. Ambient is a stand-in for bounced light from
everywhere at once, so it belongs to the scene, not to any one lamp.

The uniforms: the per-object ones from chapter 09, the directional light and
the spotlight as plain values, and the buffer of point lights. The buffer is
listed in `EACP_SHADER` like any other member:

```cpp
    Uniform<Float3> sunDirection;
    Uniform<Float3> sunColor;

    // A storage buffer of PointLight records, bound whole and read by index.
    Uniform<InputBuffer> pointLights;

    Uniform<Float3> spotPosition;
    Uniform<Float3> spotDirection;
    Uniform<Float3> spotColor;
    Uniform<Float3> spotAttenuation;
    Uniform<Float2> spotCutoffs; // cos(inner), cos(outer)
```

The light data itself is a plain array on the CPU. The marker cubes are drawn
from the same array, so a light's marker is always where the light is and in
its colour:

```cpp
const std::array<PointLight, pointLightCount> pointLightData = {{
    {{-4.0f, 1.2f, -1.5f}, {1.0f, 0.25f, 0.2f}, {1.0f, 0.22f, 0.20f}},
    {{3.5f, 1.2f, -3.5f}, {0.25f, 1.0f, 0.35f}, {1.0f, 0.22f, 0.20f}},
    {{-2.0f, 1.2f, 3.5f}, {0.3f, 0.45f, 1.0f}, {1.0f, 0.22f, 0.20f}},
    {{4.5f, 1.2f, 2.5f}, {1.0f, 0.7f, 0.2f}, {1.0f, 0.22f, 0.20f}},
}};
```

The view uploads it once, into a buffer made for shader reads
(`BufferUsage::Storage`; the default is a vertex buffer), and points the
shader at it:

```cpp
    LightCastersView()
        : pointLightBuffer(Device::shared().makeBuffer(
              pointLightData.data(), sizeof(pointLightData), BufferUsage::Storage))
    {
        // ... depth, culling and the two programs, as in chapter 09 ...

        // The lights never move, so the buffer is filled once, above, and
        // bound once here. The shader keeps a reference to it, so the buffer
        // is a member that lives as long as the shader.
        litShader.pointLights = pointLightBuffer;
```

Filling the buffer once matters. The GPU reads it while drawing frames the
CPU has already moved past, so changing a buffer every frame needs care that
a uniform does not (the GPU README's "Writing a buffer the GPU may be reading"
covers it). Uniforms are copied into each draw as it is recorded, which is
why the moving spotlight lives in uniforms and the still point lights in a
buffer.

That copy-per-draw is also what lets one program draw the whole scene. The
ground and all fourteen cubes are the same cube mesh and the same `LitShader`,
drawn fifteen times in one pass with a different model matrix each time:

```cpp
    void drawLit(RenderPass& pass, const Mat4& model, const Vec3& color)
    {
        litShader.model = model;
        litShader.normalMatrix = model.inverted().transposed();
        litShader.objectColor = color;
        pass.draw(litShader);
    }
```

The ground is a cube squashed into a wide, thin slab with
`Mat4::scale({18.0f, 0.2f, 18.0f})`. That is a non-uniform scale, so it
goes through chapter 09's normal matrix as a matter of course (for a cube's
axis-aligned faces it happens not to matter, as chapter 09 noted).

Finally `render` aims the spotlight. It hangs above the back of the scene and
its target on the ground traces a slow figure of eight; the direction is just
target minus position, and the shader normalises it. The cutoffs are turned
into cosines here, once per frame, rather than once per pixel:

```cpp
        auto spotPosition = Vec3 {0.0f, 5.5f, -4.0f};
        auto spotTarget = Vec3 {
            5.5f * std::sin(0.45f * elapsed), 0.0f, 4.5f * std::sin(0.9f * elapsed)};

        // ...

        litShader.spotPosition = spotPosition;
        litShader.spotDirection = spotTarget - spotPosition;
        litShader.spotColor = Vec3 {1.0f, 0.95f, 0.85f};
        litShader.spotAttenuation = Vec3 {1.0f, 0.0f, 0.004f};
        litShader.spotCutoffs =
            Vec2 {std::cos(radians(14.0f)), std::cos(radians(20.0f))};
```

## What you should see

A grey ground covered in pale cubes under dim blue moonlight. Four small
coloured markers (red, green, blue and amber) sit low between the cubes, each
in a pool of its own colour that fades smoothly into the dark, tinting the
faces of the nearby cubes. Where two pools overlap the colours add. A bright,
warm spotlight, its marker hanging high at the back, sweeps a soft-edged oval
across the ground, lighting up the tops and sides of cubes as it passes.

You may notice the spotlight lights a cube even when another cube stands
between it and the spot. Nothing here knows about **shadows**: each pixel
asks only "which way is the light, and how far?", never "is anything in the
way?". Answering that takes the scene drawn a second time from the light's
point of view, which is chapter 20 (Shadow Mapping).

## Try it yourself

1. Turn the lights off one kind at a time by deleting a line from
   `define()`: the directional light, the point-light loop, the spotlight.
   *Hint: with only the directional light, the scene is the flat, dim look of
   "moonlight"; it is the other lights that give it depth.*
2. Set the inner and outer cutoffs to the same angle, then pull them far
   apart (5 and 30 degrees). *Hint: with equal angles the denominator in the
   cone formula is zero; guard it, or keep them at least a fraction of a
   degree apart.*
3. Change the point lights' attenuation to the 7 row of the table, then the
   100 row. *Hint: with a very long reach the four colours wash over the whole
   ground and add up to a muddy grey.*
4. Turn the spotlight into a torch held by the viewer, as in a first-person
   game: put it at `cameraPosition` and point it along the camera's view
   direction. *Hint: the direction is `{0, 1, 0} - cameraPosition`, the
   point the camera looks at minus where it is.*
5. Make the number of point lights a run-time value. Add a
   `Uniform<UInt> lightCount`, and replace the C++ `for` with the EDSL's own
   loop over a mutable variable, reading the buffer at a computed index:
   ```cpp
   auto total = var(color);
   auto i = var(0u);
   loop(i < lightCount, [&] {
       UInt index = i;
       total = total.get() + pointLight(surface,
                                        pointLights.read3(3u * index),
                                        pointLights.read3(3u * index + 1u),
                                        pointLights.read3(3u * index + 2u));
       i = index + 1u;
   });
   color = total.get();
   ```
   Set `lightCount` to 2 and two of the pools disappear. *Hint: this is a
   real loop in the generated shader, run on every pixel, rather than four
   unrolled copies; it is how a renderer with a varying number of lights is
   written.*
6. Animate the point lights: give each a slow bob up and down. *Hint: the
   buffer must then change every frame. The simplest safe route is to keep
   the positions in uniforms after all, or read the GPU README section on
   writing a buffer the GPU may be reading.*

## Further reading

- EACP GPU README, [Buffers a shader stage reads](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#buffers-a-shader-stage-reads): `Uniform<InputBuffer>` on a `ShaderProgram`, and `read2`/`read3`/`read4`.
- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has): `var`, `loop`, `select`, constant arrays, and structs of handles.
- EACP GPU README, [Writing a buffer the GPU may be reading](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#writing-a-buffer-the-gpu-may-be-reading), before you animate data held in a buffer.
- learnopengl.com, [Light casters](https://learnopengl.com/Lighting/Light-casters) and [Multiple lights](https://learnopengl.com/Lighting/Multiple-lights).
- The Ogre3D wiki's [point light attenuation](https://wiki.ogre3d.org/tiki-index.php?page=-Point+Light+Attenuation) table, where the numbers above come from.

Previous: [12. Lighting Maps](../12-LightingMaps/README.md) | Next: [14. Stencil Testing](../14-StencilTesting/README.md)
