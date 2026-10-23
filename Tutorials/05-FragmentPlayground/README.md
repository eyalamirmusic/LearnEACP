# 05. Fragment Playground

In this chapter you stop drawing *shapes made of triangles* and start
drawing *pictures made of maths*. You cover the whole window with two
triangles and then let the fragment shader decide the colour of every single
pixel on its own: a sky gradient, a sun with a perfectly round, antialiased
edge, a thin halo ring, a moon orbiting it, and a horizon cutting through it
all, animated with time. No extra geometry is involved. By the end you will
think of a fragment shader as a function from a pixel's position to a colour,
you will know the small toolkit of `step`, `smoothstep`, `mix`, `length`,
`abs` and `min` that most procedural graphics is built from, and you will know
how to keep a circle round in a window that is not square.

## The fragment shader is a function

Recall the pipeline: the vertex stage places corners, the **rasterizer**
works out which pixels each triangle covers, and the fragment stage runs once
for each of those pixels (each **fragment**). If the triangles cover the whole
window, the fragment stage runs once for *every* pixel. At that point the
geometry hardly matters any more; what you draw is decided by

```
colour = f(position of this pixel)
```

and all you have to do is write `f`. This is how a lot of real graphics works:
post-processing effects, procedural backgrounds, signed-distance UI, and every
demo on Shadertoy.

### A full-screen quad with UVs

Clip space runs from -1 to 1 on both axes, so a quad with corners at (±1, ±1)
covers the window exactly. Each corner also carries a **UV** coordinate, a
2D position that runs from 0 to 1 across the quad. As a `varying`, the UV is
interpolated by the rasterizer, so every fragment receives its own position
in that 0..1 square:

```
 clip space                          uv (what each fragment receives)

 (-1, 1) +------------+ (1, 1)       (0, 1) +------------+ (1, 1)
         |            |                     |            |
         |   (0, 0)   |                     | (0.5, 0.5) |
         |            |                     |            |
 (-1,-1) +------------+ (1,-1)       (0, 0) +------------+ (1, 0)
```

In this chapter we choose UV (0, 0) to be the bottom-left corner, so `uv.y`
grows upwards like clip space does. That is our choice, made in the vertex
data; there is nothing special about it.

## The toolkit

Everything in this chapter is built from six small functions. All of them work
on a `Float` or componentwise on a vector, and all of them exist on every
shading language EACP targets.

**`mix(a, b, t)`** is linear interpolation: `a` when `t` is 0, `b` when `t` is
1, and proportionally in between. With colours it is a cross-fade; with a 0/1
mask it is a "choose".

**`step(edge, x)`** is a hard threshold: 0 where `x < edge`, 1 elsewhere.

**`smoothstep(e0, e1, x)`** is a soft threshold: 0 below `e0`, 1 above `e1`,
and a smooth S-curve between them.

```
 step(0.5, x)              smoothstep(0.4, 0.6, x)

 1 |        +-------       1 |          .-------
   |        |                |         /
 0 +--------+                0 +-------'
   0       0.5      1          0     0.4 0.6    1
```

**`length(v)`** is the length of a vector: `length(p)` is the distance from
`p` to the origin.

**`abs(x)`** folds negative values onto positive ones.

**`min(a, b)`** is the smaller of two values.

### Signed distance

A **signed distance function** (SDF) tells you, for any point, how far it is
from the edge of a shape: negative inside, zero exactly on the edge, positive
outside. For a circle of radius `r` around the origin it is one line:

```
d = length(p) - r

          +0.2
      .---------.
     /   -0.1    \         d < 0  inside
    |      .  r   |0       d = 0  on the edge
     \           /         d > 0  outside
      '---------'
```

The beauty of distances is that you can combine and reshape them before you
ever turn them into a colour:

- **Ring:** `abs(d) - w`. Taking `abs` makes the distance zero *on* the old
  edge and positive on both sides, so subtracting a width `w` turns the old
  outline into a band of thickness `2w`.
- **Union:** `min(d1, d2)`. A point is inside the union if it is inside
  either shape, and the nearest edge of the union is whichever edge is
  nearer. Both of those are exactly what `min` computes.

To turn a distance into coverage you threshold it: `step(d, 0.0)` would give a
hard edge, but `1.0 - smoothstep(0.0, pixel, d)` blends over the width of one
pixel, which is antialiasing for free.

## Keeping circles round: resolution and aspect ratio

UV runs 0..1 on *both* axes regardless of the window's shape. In an 800×500
window one unit of `uv.x` covers 800 pixels and one unit of `uv.y` covers
500, so `length(uv - 0.5)` measures horizontal distance in different units
from vertical distance, and a "circle" comes out as an ellipse.

The fix is to rescale one axis by the window's **aspect ratio**
(width / height):

```
 centered = uv * 2 - 1                    -1..1 on both axes (stretched)
 p        = (centered.x * aspect, centered.y)

          +-----------------------------------+  p.y =  1
          |                                   |
          |                 o                 |  p.y =  0
          |                                   |
          +-----------------------------------+  p.y = -1
     p.x = -aspect                      p.x = +aspect
```

Now one unit is the same number of pixels in both directions, and `length(p)`
is a true distance.

To know the aspect ratio the shader needs the size of what it is drawing into,
which is a perfect job for a uniform: `Uniform<Float2> resolution`. The view
reads it from the frame:

```cpp
        // Read the size off the frame every time: it is the size of what is
        // being drawn into right now, in device pixels, so it is right during
        // a live resize and on a high-density display.
        auto size = frame.pixelSize();
        shader.resolution = Vec2 {size.x, size.y};
        shader.time = seconds;
```

A word on **points and pixels**. Window sizes in a desktop OS are measured in
logical *points*; on a high-density ("Retina") display one point is two or
more physical pixels. `frame.pixelSize()` is the drawable's size in real
device pixels, `frame.logicalSize()` is the same size in points, and
`frame.backingScale()` (also available on the view as `backingScale()`) is the
ratio between them. For the aspect ratio either would do, but the antialiasing
below needs to know how big one *pixel* is, so pixels are what we send.

## Walking through the code

### The quad

```cpp
struct Vertex
{
    Vec2 position; // clip space, -1..1
    Vec2 uv; // 0..1 across the window, (0, 0) at the bottom left
};

// A quad that exactly covers clip space, so it covers the whole window.
const Vertex screenVertices[] = {
    {{-1.0f, -1.0f}, {0.0f, 0.0f}}, // bottom left
    {{1.0f, -1.0f}, {1.0f, 0.0f}}, // bottom right
    {{1.0f, 1.0f}, {1.0f, 1.0f}}, // top right
    {{-1.0f, 1.0f}, {0.0f, 1.0f}}, // top left
};

const std::uint16_t screenIndices[] = {0, 1, 2, 0, 2, 3};
```

This is the indexed quad from chapter 03 (Interpolation) stretched to the edges of clip space,
with a UV beside each position.

### The vertex stage, and getting into the fragment stage

```cpp
        // The vertex stage has nothing to do but pass the corners through.
        auto position = vertexInput(&Vertex::position);
        setPosition(float4(position, 0.0f, 1.0f));

        // Everything below runs once per pixel.
        auto uv = varying(vertexInput(&Vertex::uv));

        // An all-literal colour needs constant() to put it on the graph.
        auto rgb = [this](float r, float g, float b)
        { return float3(constant(r), g, b); };
```

`varying(...)` is the doorway between the stages: everything computed from
`uv` from here on is computed per fragment.

The little `rgb` helper is an EDSL detail. `float3(...)` builds a vector from
handles and literals, but it needs at least one handle to know which shader
graph the result belongs to. `float3(1.0f, 0.5f, 0.2f)` has none, so
`constant(r)` turns the first literal into a handle and the rest can stay
plain floats. Because `define()` is ordinary C++, a lambda is a perfectly good
way to tidy that up.

### 1. A gradient

```cpp
        // 1. A gradient: uv.y is 0 at the bottom and 1 at the top, so mix()
        //    walks from the first colour to the second up the window.
        auto color = mix(rgb(0.95f, 0.45f, 0.35f), rgb(0.10f, 0.08f, 0.30f), uv.y());
```

The simplest possible fragment function: the colour depends only on height.
Peach at the bottom, deep blue at the top, a smooth fade between. `color` is
a C++ variable holding a handle, and the following steps will reassign it,
layering more onto the picture. Each reassignment just points `color` at a new
node in the graph.

### 2. Aspect-corrected coordinates

```cpp
        // 2. Aspect-corrected coordinates. p runs -1..1 vertically, and
        //    horizontally by the same scale, so a circle stays round in a
        //    window of any shape.
        auto aspect = resolution.x() / resolution.y();
        auto centered = uv * 2.0f - 1.0f;
        auto p = float2(centered.x() * aspect, centered.y());

        // One pixel measured in p units: p spans 2 units over the height.
        auto pixel = 2.0f / resolution.y();
```

This is the correction from the section above. `.x()` and `.y()` are
**swizzles**: they pick components out of a vector. (Swizzles combine too:
`.xy()`, `.yx()`, `.xxy()` and so on are all available.) `pixel` is the size
of one device pixel in `p`'s units; you will use it as the width of every soft
edge so that edges are exactly one pixel wide on any display.

### 3. A circle

```cpp
        // 3. A signed-distance circle: negative inside, zero on the edge,
        //    positive outside. Its radius breathes with time.
        auto sunCenter = float2(constant(0.0f), -0.25f);
        auto toSun = p - sunCenter;
        auto radius = 0.45f + 0.03f * sin(time * 2.0f);
        auto sun = length(toSun) - radius;

        // smoothstep turns the distance into coverage with an edge one pixel
        // wide, which is antialiasing for free.
        auto sunMask = 1.0f - smoothstep(0.0f, pixel, sun);
        auto sunColor =
            mix(rgb(1.0f, 0.35f, 0.25f), rgb(1.0f, 0.85f, 0.35f), uv.y());
        color = mix(color, sunColor, sunMask);
```

Subtracting the centre moves the circle: `toSun` is the pixel's position
relative to the sun. The radius uses the `time` uniform from chapter 04 (Uniforms), so the
sun gently grows and shrinks. `sunMask` is 1 inside, 0 outside, with a one
pixel ramp at the edge, and `mix(color, sunColor, sunMask)` paints the sun
over the sky exactly where the mask is 1. The sun gets its own vertical
gradient so it is not a flat disc.

Try replacing the `smoothstep` with `step(sun, 0.0f)` and look closely at the
edge: you will see the staircase that `smoothstep` is hiding.

### 4. A ring

```cpp
        // 4. A ring is the circle's distance folded with abs(): zero along a
        //    circle of the halo's radius, minus half its thickness.
        auto halo = abs(length(toSun) - (radius + 0.12f)) - 0.012f;
```

`length(toSun) - (radius + 0.12f)` is a circle slightly larger than the sun.
`abs` turns it into "distance to that circle's outline", and subtracting
0.012 gives the outline a thickness of 0.024. Because the halo uses the same
`radius`, it breathes with the sun.

### 5. A moon, and a union

```cpp
        // 5. A second circle: a moon that circles the sun once every 2 pi
        //    (about six) seconds.
        auto orbit = float2(cos(time), sin(time)) * 0.75f;
        auto moon = length(toSun - orbit) - 0.07f;

        // min() of two distances is the distance to whichever is nearer: the
        // union of the two shapes, drawn with one mask.
        auto outlines = min(halo, moon);
        auto outlineMask = 1.0f - smoothstep(0.0f, pixel, outlines);
        color = mix(color, rgb(1.0f, 0.95f, 0.85f), outlineMask);
```

`(cos t, sin t)` walks around the unit circle as `t` grows, so `orbit` moves
the moon's centre around the sun on a circle of radius 0.75. Then `min`
combines the halo and the moon into one shape, and one `smoothstep` and one
`mix` draw both of them. With SDFs, adding a shape to a scene is one more
`min`.

### 6. A hard horizon

```cpp
        // 6. A hard horizon. step(edge, x) is 0 below the edge and 1 above,
        //    with nothing in between, so this edge is visibly jagged.
        auto horizon = 1.0f - step(0.375f, uv.y());
        color = mix(color, rgb(0.06f, 0.05f, 0.12f), horizon);

        setFragment(float4(color, 1.0f));
```

`step(0.375f, uv.y())` is 1 above the line and 0 below it; one minus that is
the ground mask. It is painted last, so it covers the bottom half of the sun,
halo and moon: in this style of shader, **later layers are drawn on top**.
`uv.y = 0.375` is `p.y = -0.25`, exactly the sun's centre, so the sun sits
half-set on the horizon.

The horizon is horizontal, so it falls exactly along a row of pixels and
looks clean. Tilt it (compare `uv.y()` against something that also depends on
`uv.x()`) and the jaggies of a hard `step` appear immediately.

### The uniforms and the view

```cpp
    // The drawable's size in pixels, and the time in seconds.
    Uniform<Float2> resolution;
    Uniform<Float> time;

    EACP_SHADER(resolution, time)
```

Two uniforms, both listed in `EACP_SHADER`. A `Uniform<Float2>` accepts a
`Maths::Vec2` directly, which is what `render()` assigns. The rest of the view
is the continuous-mode animation from the previous chapter:

```cpp
    void update(Threads::FrameTime frameTime) override
    {
        seconds += static_cast<float>(frameTime.delta);
    }
```

Note that `beginPass({})` uses the default clear colour; it hardly matters,
since the quad paints over every pixel.

## Try it yourself

1. Swap the gradient's direction so it runs left to right. *Hint: use
   `uv.x()` in the first `mix`.*
2. Comment out the aspect correction (use `centered` instead of `p`) and
   resize the window. *Hint: the sun turns into an ellipse that follows the
   window's shape.*
3. Add a second moon on the opposite side of the orbit, moving twice as fast.
   *Hint: one more `length(...) - r` and one more `min`.*
4. Make the horizon wavy. *Hint: compare `uv.y()` against
   `0.375f + 0.02f * sin(uv.x() * 20.0f + time)`, and use `smoothstep` with a
   width of `pixel` instead of `step` so the waves are smooth.*
5. Tile the sky with stars or a grid. *Hint: `fract(p * 8.0f)` repeats the
   0..1 range eight times; draw a small circle around the centre of each cell
   with `length(fract(p * 8.0f) - 0.5f)`.*

## Further reading

- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has):
  the full list of types, swizzles and intrinsics you can use in `define()`.
- EACP GPU README, [A shader](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#a-shader):
  a full-screen waves example in the same style.
- [The Book of Shaders](https://thebookofshaders.com/): the best gentle
  introduction to fragment-shader thinking, with chapters on
  [shaping functions](https://thebookofshaders.com/05/) and
  [shapes](https://thebookofshaders.com/07/).
- Inigo Quilez, [2D distance functions](https://iquilezles.org/articles/distfunctions2d/):
  a catalogue of SDFs for nearly every 2D shape.

Previous: [04. Uniforms](../04-Uniforms/README.md) | Next: [06. Textures](../06-Textures/README.md)
