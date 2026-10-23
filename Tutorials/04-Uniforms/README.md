# 04. Uniforms

Up to now every number your shaders used came either from the vertex buffer or
was written into the shader as a literal. That is enough for a still picture,
but nothing can change once the program is running. In this chapter you give
the shader a **uniform**: a value you set from C++ every frame and that every
vertex and every fragment of the draw sees. You will use one uniform, the time
in seconds, to spin the coloured quad from the previous chapter in the vertex
stage and to make its colours pulse in the fragment stage. Along the way you
will learn how EACP finds a shader's uniforms, how to turn on continuous
rendering, and why animation should be driven by elapsed time rather than by
counting frames.

## Two ways to get data into a shader

So far you have one way in: **vertex inputs**. Every field of your `Vertex`
struct is a vertex input, and the vertex stage runs once per vertex with that
vertex's own values. Four corners, four positions, four colours.

Some data is not like that. The current time, the size of the window, a
camera matrix, a tint applied to a whole object: there is exactly *one* of
each for the whole draw, and every vertex and every fragment wants the same
value. Storing a copy in every vertex would waste memory, and worse, you
would have to rewrite the whole vertex buffer just to change it.

That is what a **uniform** is: a value that is *uniform* across one draw call.
You write it on the CPU before the draw, the GPU reads it from a small block
of memory, and every invocation of every stage sees the same number.

```
               CPU (your C++)                              GPU
  +-----------------------------------+
  | vertex buffer (set once)          | --- one value per vertex --> vertex stage
  |   {pos, color} {pos, color} ...   |                                  |
  +-----------------------------------+                             rasterizer
  | uniform block (set every frame)   | --- same value for all ---->     |
  |   time = 3.27                     |     vertices and fragments   fragment stage
  +-----------------------------------+
```

A quick comparison:

| | Vertex input | Varying | Uniform |
| --- | --- | --- | --- |
| Where it comes from | the vertex buffer | the vertex stage's output | your C++ code, per draw |
| How many values | one per vertex | one per fragment (interpolated) | one per draw |
| Who can read it | vertex stage | fragment stage | either stage, or both |
| When it changes | when you upload new vertices | every fragment | whenever you assign it |

## What a uniform looks like in EACP

In the EDSL a uniform is a data member of your shader struct:

```cpp
    // A uniform: one value for the whole draw, written from C++ before
    // pass.draw(). Not part of Vertex, so it has no per-vertex storage.
    Uniform<Float> time;

    // Lists the uniform members so compile() can find them by name.
    EACP_SHADER(time)
```

`Uniform<Float>` is two things at once, and that is the trick that makes it
pleasant to use:

- Inside `define()` it is a `Float` **handle**, exactly like the handles
  `vertexInput` gives you. Using it in an expression (`time * 0.8f`) records a
  node in the shader graph that means "read the `time` field of the uniform
  block".
- Outside `define()` it is a typed **CPU slot**. `shader.time = 3.27f` stores
  that float in the member, and when you call `pass.draw(shader)` EACP packs
  every uniform's current value into the uniform block and hands it to the GPU.

`EACP_SHADER(time)` is how EACP learns which members are uniforms. C++ has no
way for a base class to discover the data members of a derived class, so the
macro writes that list out for it. When your constructor calls `compile()`,
`compile()` walks the listed members in order, gives each one a name and an
offset in the uniform block, and only then runs `define()` to record the
graph. Because the uniforms were registered first, `define()` can use them like
any other value. If you add a uniform member and forget to list it,
`compile()` never registers it and the shader cannot use it, so keep the list
and the members together. With several uniforms the macro takes them all:
`EACP_SHADER(time, resolution)`.

### Which stage gets the uniform

Notice that you never say "this uniform is for the vertex stage". EACP works it
out. When it emits the shader it looks at which stage's graph actually reads
each uniform, declares the uniform block in that stage, and binds it only
there when you draw. In this chapter `time` is read in both stages, so both
receive the block. If you deleted the pulse from the fragment stage, the
fragment function would simply stop declaring it.

### What cannot be a uniform

Most shader types can: `Float`, `Float2/3/4`, `Float4x4`, `Int` and the
integer vectors, and (as you will see in chapter 06, Textures) textures. Two kinds are
refused at compile time with a `static_assert`:

- **`Bool` and the boolean vectors.** Metal packs a `bool` into one byte while
  an HLSL constant buffer gives it four. No amount of padding *between* fields
  can fix a disagreement about the size *of* a field, so the same C++ struct
  could not feed both. Send a `Float` (or an `Int`) and compare it in the
  shader instead.
- **`Float2x2` and `Float3x3`.** Metal stores a `float2x2` as two packed
  columns (16 bytes); HLSL gives every matrix row its own 16-byte register (32
  bytes). Send a `Float4x4`, which both agree on.

This is a pattern you will see throughout EACP: rather than letting the same
code mean different things on Metal, Direct3D and Vulkan, the EDSL refuses the
cases where the backends disagree.

## Animation and the render loop

By default a `GPUView` only renders when it has to, for example when the
window is resized or uncovered. That is ideal for a still image (it costs
nothing while nothing changes) but useless for animation. One call changes it:

```cpp
        // Render every display refresh instead of only when something asks
        // for a repaint, and call update() before each frame.
        setContinuous(true);
```

In continuous mode the view is driven by the display's refresh signal
(vsync). Before each frame it calls `update(Threads::FrameTime)`, where you
advance your animation state, and then `render(Frame&)`, where you draw it.

### Why delta time, not frame count

The tempting way to animate is "rotate by 0.01 radians every frame". The
problem is that frames do not come at a fixed rate. A standard monitor
refreshes 60 times a second, many laptops and phones 120, and a frame that
takes too long is simply skipped. Animate per frame and your quad spins twice
as fast on a 120 Hz screen, and stutters every time a frame is dropped.

Instead, measure **time**. `FrameTime::delta` is the number of seconds since
the previous `update`, so adding it up gives a clock that runs at real-world
speed whatever the refresh rate:

```
  60 Hz:  |--16.7ms--|--16.7ms--|--16.7ms--|     3 frames, +0.050 s
 120 Hz:  |-8.3-|-8.3-|-8.3-|-8.3-|-8.3-|-8.3-|  6 frames, +0.050 s
 hitch:   |--16.7ms--|-------33.3ms-------|      2 frames, +0.050 s
```

All three rows advance the clock by the same amount, so the quad is at the
same angle at the same moment on every machine. Any speed in your shader is
then naturally "per second": `time * 0.8f` is 0.8 radians per second.

## Walking through the code

The vertex data is the indexed quad you built in chapter 03 (Interpolation), with a colour per
corner:

```cpp
// The quad from the previous chapter: four corners, each with its own colour.
struct Vertex
{
    Vec2 position;
    Vec3 color;
};

const Vertex quadVertices[] = {
    {{-0.5f, -0.5f}, {1.0f, 0.3f, 0.3f}}, // bottom left
    {{0.5f, -0.5f}, {0.3f, 1.0f, 0.3f}}, // bottom right
    {{0.5f, 0.5f}, {0.3f, 0.3f, 1.0f}}, // top right
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 0.3f}}, // top left
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};
```

The constructor does what you have seen before; the comment says what
`compile()` now does with the uniform:

```cpp
    // compile() walks the members EACP_SHADER names, gives each uniform a slot
    // in the uniform block, then calls define() to record the graph.
    SpinningShader() { compile(); }
```

### The vertex stage: rotation

```cpp
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);

        // Vertex stage: `time` is the same number for all four corners, so
        // every corner turns by the same angle and the quad spins as one.
        // This is the standard 2D rotation:
        //   x' = x cos(a) - y sin(a)
        //   y' = x sin(a) + y cos(a)
        auto angle = time * 0.8f; // radians: 0.8 per second
        auto c = cos(angle);
        auto s = sin(angle);
        auto rotated = float2(position.x() * c - position.y() * s,
                              position.x() * s + position.y() * c);

        setPosition(float4(rotated, 0.0f, 1.0f));
```

Remember that `define()` runs once, on the CPU, when the shader is compiled.
`time * 0.8f` does not multiply anything right now; it records "read `time`,
multiply by 0.8" into the graph, and the GPU performs that multiplication for
every vertex, every frame, with whatever value `time` holds at the time of the
draw.

The rotation formula turns a point `(x, y)` around the origin by angle `a`.
Since all four corners use the same `a`, the quad rotates rigidly. This is the
key property of a uniform: if `angle` came from the vertex buffer instead,
each corner could turn by a different amount and the quad would tear itself
apart.

Chapter 07 (Transformations) will write this same rotation as a matrix. For now the two lines of
trigonometry show exactly what is going on.

### The fragment stage: a pulse

```cpp
        // Fragment stage: the interpolated colour is scaled by a brightness
        // that swings between 0.4 and 1.0 a little under once a second.
        // Reading `time` here as well puts the uniform block in both stages.
        auto pulse = 0.7f + 0.3f * sin(time * 5.0f);
        setFragment(float4(varying(color) * pulse, 1.0f));
```

`sin` swings between -1 and 1, so `0.7 + 0.3 * sin(...)` swings between 0.4
and 1.0. `sin(time * 5.0f)` completes a cycle every 2π/5 ≈ 1.26 seconds.
Multiplying the interpolated colour by a scalar scales all three channels,
which darkens and brightens the quad without changing its hues.

Here `time` is read in the fragment stage. The fragment stage is where colour
is decided, so effects on colour belong here; the vertex stage is where
position is decided, so motion belongs there. One uniform can serve both.

### The view: setting the uniform every frame

```cpp
    UniformsView()
    {
        shader.setVertices(quadVertices);
        shader.setIndices(quadIndices);
        shader.prepare(sampleCount());

        // Render every display refresh instead of only when something asks
        // for a repaint, and call update() before each frame.
        setContinuous(true);
    }

    // Advance the clock by how long the last frame actually took, so the
    // animation runs at the same speed at 60 Hz, 120 Hz or after a hitch.
    void update(Threads::FrameTime frameTime) override
    {
        seconds += static_cast<float>(frameTime.delta);
    }
```

`seconds` is an ordinary C++ float in the view. It is your application state;
the shader never sees it directly. `FrameTime` also carries an absolute
`time`, but accumulating `delta` yourself keeps you in control: you can pause
the clock, slow it down or rewind it simply by changing what you add.

```cpp
    void render(Frame& frame) override
    {
        // Copy the CPU value into the uniform; draw() uploads it.
        shader.time = seconds;

        auto pass = frame.beginPass({Graphics::Color {0.10f, 0.10f, 0.12f}});
        pass.draw(shader);
    }
```

The order matters: assign uniforms first, then draw. `pass.draw(shader)` takes
a snapshot of every uniform's value at that moment and uploads it with the
draw. Assigning after the draw would only affect the next frame.

Run it and you will see the quad turning and breathing. You may also notice
that the quad looks squashed into a diamond as it turns, unless your window is
square. That is because clip space always runs from -1 to 1 on both axes,
however wide the window is. You will fix that with an aspect-ratio correction
in the next chapter.

## Try it yourself

1. Make the quad spin the other way, twice as fast. *Hint: only the constant
   in `angle` needs to change.*
2. Add a pause: stop advancing `seconds` after five seconds of running.
   *Hint: it is pure C++ in `update()`; the shader does not change.*
3. Add a second uniform, `Uniform<Float> speed`, list it in `EACP_SHADER`, and
   use it in place of `0.8f`. Set it from `render()`. *Hint:
   `EACP_SHADER(time, speed)`.*
4. Make each corner pulse out of step with the others. *Hint: you need
   something per vertex mixed with the uniform, for example
   `sin(time * 5.0f + position.x() * 3.0f)` computed in the vertex stage and
   passed through a `varying`.*
5. Try declaring `Uniform<Bool> paused;` and read the compiler error, then
   implement the same idea with a `Uniform<Float>` and `step` or `mix`.
   *Hint: `mix(a, b, pausedFloat)` picks `a` at 0 and `b` at 1.*

## Further reading

- EACP GPU README, [A shader](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#a-shader):
  what `compile()`, `prepare()` and `draw()` do, and how the uniform block is
  bound only to the stages that read it.
- EACP GPU README, [What it deliberately refuses](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-it-deliberately-refuses):
  the full reasoning behind the refused uniform types.
- [`Apps/GPU/RotatingTriangle`](https://github.com/eyalamirmusic/eacp/blob/main/Apps/GPU/RotatingTriangle/Main.cpp):
  the EACP example this chapter is based on.
- learnopengl.com, [Shaders](https://learnopengl.com/Getting-started/Shaders):
  uniforms in OpenGL, for comparison.
- Glenn Fiedler, [Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/):
  the classic article on frame time, delta time and stable simulation.

Previous: [03. Interpolation](../03-Interpolation/README.md) | Next: [05. Fragment Playground](../05-FragmentPlayground/README.md)
