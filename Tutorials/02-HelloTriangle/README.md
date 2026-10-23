# 02. Hello Triangle

In the last chapter the GPU cleared a window to one colour. Now it draws
something: a single orange triangle on a dark background. Getting there takes
your first **shader**, a small program that runs on the GPU. By the end of this
chapter you will know the stages a triangle goes through on its way to the
screen (the graphics pipeline), the coordinate system the GPU expects positions
in (clip space), what a vertex and a fragment are, and why in EACP a shader is a
C++ struct rather than a string of shader-language source.

## The graphics pipeline

A GPU does not draw shapes the way a paint program does. It draws **triangles**,
and every triangle goes through the same fixed sequence of steps, called the
**graphics pipeline**. Some steps are built into the hardware; two of them run
code you write.

```
  vertex buffer          VERTEX STAGE          RASTERIZER           FRAGMENT STAGE        image
  (your data)            (your code)           (fixed hardware)     (your code)

  v0 (0.0,  0.5)  --->  where is v0?  --\
  v1 (-0.5,-0.5)  --->  where is v1?  ---+-->  which pixels   --->  what colour is   ---> pixel
  v2 (0.5, -0.5)  --->  where is v2?  --/      does the            this pixel?           written
                                               triangle cover?     (once per pixel)
       3 runs                                  -> fragments         thousands of runs
```

- A **vertex** is one corner of a triangle, together with whatever data you
  attach to it. Here, a vertex is just a 2D position. Later chapters add
  colours, texture coordinates and normals.
- The **vertex stage** (the *vertex shader*) runs once per vertex. Its job is to
  say where that vertex lands on screen. Our triangle has three vertices, so it
  runs three times.
- The **rasterizer** is fixed-function hardware. It takes the three placed
  corners, works out which pixels the triangle covers, and produces one
  **fragment** for each. A fragment is a candidate pixel: a position on the
  image plus any data carried over from the vertices. (Why not just "pixel"?
  A fragment can still be thrown away before it becomes a pixel, and with
  multisampling one pixel can be built from several samples.)
- The **fragment stage** (the *fragment shader*, called a *pixel shader* in
  Direct3D) runs once per fragment and returns that fragment's colour. A
  triangle covering a quarter of a 1280x800 window runs it a quarter of a
  million times, all in parallel.

The program that holds a vertex stage and a fragment stage is what graphics
people call a **shader** (or *shader program*). Together with fixed settings
such as the image format and the multisample count, it forms a **pipeline**
object that the GPU can draw with.

## Clip space and normalized device coordinates

The vertex stage reports a position, but in what units? Not pixels: the window
could be any size, on a Retina display or not. The GPU wants positions in
**clip space**, a coordinate system where the visible part of the window always
spans **-1 to 1** on both axes, with **x to the right and y up**, regardless of
the window's size or shape.

The vertex stage actually outputs four numbers, `(x, y, z, w)`. The GPU divides
the first three by `w` to get **normalized device coordinates** (NDC). With
`w = 1`, as in this chapter, clip space and NDC are the same thing, and you can
think of `x` and `y` as positions in the square below. (`z` is depth, which
matters once we draw in 3D; `w` matters for perspective. Both come back in
chapters 07 (Transformations) and 08 (Coordinate Systems).)

Here is where our three vertices land:

```
                       y
                      +1 +-----------------------------+
                         |                             |
                         |            v0               |
                    0.5  |          (0, 0.5)           |
                         |            /\               |
                         |           /  \              |
                     0 --|----------/----\-------------|-- x
                         |         /      \            |
                   -0.5  |    v1 /________\ v2         |
                         |  (-0.5,-0.5)  (0.5,-0.5)    |
                         |                             |
                      -1 +-----------------------------+
                        -1                            +1
```

Anything outside the -1..1 square is **clipped**: cut off, never drawn. That is
where "clip space" gets its name. After clipping, the GPU stretches the square
to the window's pixel size (the *viewport transform*), so `(-1, -1)` becomes
the bottom-left pixel and `(1, 1)` the top-right one.

Notice the consequence: the square is stretched to fit the window. The default
window is 640x400 points, wider than it is tall, so a triangle that is one unit
wide and one unit tall in clip space is wider than it is tall on screen. That
is expected. Correcting for the window's shape (its *aspect ratio*) is
something you do yourself, in a later chapter.

## Why the shader is a C++ struct

Every GPU API has its own shader language. Metal uses MSL, Direct3D uses HLSL,
Vulkan consumes SPIR-V that is usually compiled from GLSL. The traditional
approach is to write the shader in one of those languages as a separate file or
a string, compile it at run time, and describe by hand how your C++ vertex
struct lines up with the shader's inputs. Three platforms means three copies of
every shader, and the C++ and shader sides can drift apart silently.

EACP takes a different approach. You write the shader in C++, as a struct
deriving from `ShaderProgram`, using EACP's shader **EDSL** (embedded
domain-specific language):

1. You override **`define()`**. Inside it, functions like `vertexInput`,
   `float4`, `setPosition` and `setFragment` do not compute anything. They
   return and take **handles**: small objects that stand for a value the GPU
   will compute later. Each call adds a node to a **graph** that describes the
   shader.
2. **`compile()`**, called from your shader's constructor, first walks the
   shader's uniforms (values you set from C++; we have none yet), then calls
   `define()` once, on the CPU, to record the graph.
3. **Emitters** turn the graph into source text for the platform you are on:
   MSL on macOS, HLSL on Windows, GLSL on Linux. Your program never contains a
   shader string.
4. **`prepare()`** hands that source to the GPU driver, which compiles it into
   machine code, and builds the pipeline.

So a mistake like adding a `Float2` to a `Float3` is a C++ compile error, and
the vertex layout is read from your actual C++ struct, so it cannot disagree
with the data you upload. One thing does still happen at run time: the driver
compiles the generated source inside `prepare()`, so always run your program,
not just build it.

For the curious, this is the Metal source EACP emits for this chapter's shader:

```metal
struct VertexIn
{
    float2 a0 [[attribute(0)]];
};

struct VertexOut
{
    float4 position [[position]];
};

vertex VertexOut vertexMain(VertexIn input [[stage_in]])
{
    VertexOut output;
    output.position = float4(input.a0, 0.0, 1.0);
    return output;
}

fragment float4 fragmentMain(VertexOut input [[stage_in]])
{
    return float4(1.0, 0.5, 0.2, 1.0);
}
```

You can see each line of `define()` in it. You never have to read or write
this, but it is reassuring to know there is no magic: the EDSL is a way of
writing exactly this program once for every backend.

## Walking through the code

### The vertex

```cpp
struct Vertex
{
    Maths::Vec2 position;
};
```

This is the CPU's idea of a vertex: one 2D position. `Maths::Vec2` is EACP's
plain two-float vector, and the shader layer already knows it corresponds to a
`Float2` on the GPU. Because the shader reads its inputs straight from this
struct (see below), this struct *is* the vertex layout. Add a field here and
pull it in the shader, and the layout follows.

### The data

```cpp
const Vertex triangleVertices[] = {
    {{0.0f, 0.5f}}, // top
    {{-0.5f, -0.5f}}, // bottom left
    {{0.5f, -0.5f}}, // bottom right
};
```

Three vertices in clip space, exactly the ones in the diagram above. (The double
braces are one for the `Vertex` and one for its `Vec2`.)

### The shader

```cpp
struct TriangleShader final : ShaderProgram
{
    TriangleShader() { compile(); }
```

The constructor calls `compile()`, which records the shader by calling
`define()`. It must be the most-derived class that calls it, which is why it is
here and not in `ShaderProgram`'s constructor: `define()` is virtual, and a base
class constructor cannot reach a derived override yet.

```cpp
        auto position = vertexInput(&Vertex::position);
```

`vertexInput` takes a pointer to a member of your vertex struct. From it, EACP
learns the attribute's type (`Vec2`, so `Float2`), its byte offset inside
`Vertex`, and the stride between vertices (`sizeof(Vertex)`). It returns a
`Float2` handle. `position` is not a pair of numbers: it stands for "whatever
this vertex's position turns out to be", and anything you compute from it is
computed on the GPU, once per vertex.

```cpp
        setPosition(float4(position, 0.0f, 1.0f));
```

This is the vertex stage's output: the clip-space position. `float4(...)`
builds a four-component vector from a `Float2` handle and two numbers, giving
`(x, y, 0, 1)`. As described above, `z = 0` puts the triangle at depth zero and
`w = 1` makes clip space and NDC the same.

```cpp
        setFragment(float4(constant(1.0f), 0.5f, 0.2f, 1.0f));
```

And this is the fragment stage: every fragment of the triangle is the colour
`(1, 0.5, 0.2, 1)`, an opaque orange. Why `constant(1.0f)` rather than plain
`1.0f`? `float4` builds a node in the shader graph, and it finds the graph
through its handle arguments. A list of four plain C++ floats has no handle, so
it has no graph to add itself to. `constant()` turns a C++ number into a
`Float` handle, and the rest of the literals can then ride along with it. As
soon as a shader computes a colour from something (as the next chapter does),
you will not need it.

How does EACP know which stage a line belongs to? From what it feeds.
`setPosition` is the vertex stage's output, so everything it depends on runs per
vertex. `setFragment` is the fragment stage's output, so it runs per fragment.

```cpp
    // A shader normally lists its uniforms (values set from C++ per draw) with
    // EACP_SHADER(name, ...), which writes this function for you. With no
    // uniforms yet, we write the empty version by hand: EACP_SHADER() would
    // leave its parameter unused and draw a compiler warning.
    void reflectMembers(ShaderVisitor&) override {}
```

`compile()` needs to know which members of your shader are uniforms. Normally
you list them in `EACP_SHADER(...)`, as you will from chapter 04 (Uniforms) on, and the
macro writes `reflectMembers` for you. With nothing to list we write the empty
function directly: EACP's own tests write `EACP_SHADER()` here and accept the
unused-parameter warning, and this hand-written override is the same function
without it.

### The view

```cpp
    HelloTriangleView()
    {
        // Copy the vertices into a GPU buffer the shader owns.
        shader.setVertices(triangleVertices);

        // Build the GPU pipeline: compile the generated source and fix the
        // state it draws with. The sample count must match the view's.
        shader.prepare(sampleCount());
    }
```

This runs once, when the view is created.

- **`setVertices`** copies the array into a **vertex buffer**, a block of GPU
  memory the shader reads its vertices from. The program keeps the buffer and
  remembers there are three vertices. It also checks (in debug builds) that
  `sizeof(Vertex)` matches the layout `define()` pulled from it.
- **`prepare`** builds the pipeline. Its argument is the **sample count**:
  `GPUView` draws with 4x multisample anti-aliasing (MSAA) by default, which
  takes four samples per pixel along triangle edges so they look smooth rather
  than jagged. A pipeline has to be built for the same count as the image it
  draws into, so we pass the view's `sampleCount()`.

Both are slow-ish operations (memory allocation, driver compilation) that you do
once, not every frame.

```cpp
    void render(Frame& frame) override
    {
        auto pass = frame.beginPass({.clearColor = {0.10f, 0.10f, 0.12f}});

        // Bind the pipeline and vertex buffer, then draw all three vertices.
        pass.draw(shader);
    }
```

`render` is the clear from chapter 01 (Hello Window) plus one line. `pass.draw(shader)` **binds** the
shader's pipeline and vertex buffer to the pass (tells the GPU "use these for
the next draw") and issues a **draw call** for all three vertices. The GPU runs
the vertex stage three times, rasterizes the triangle, runs the fragment stage
for every covered pixel, and the orange triangle appears.

```cpp
    TriangleShader shader;
```

The shader is a member of the view, so it lives as long as the view does, and
so do the buffer and pipeline it owns.

## Try it yourself

1. Change the triangle's colour, then make it semi-transparent by lowering
   alpha. Does anything change? *Hint: blending is off by default, so alpha is
   written but not used; the view is opaque.*
2. Move one vertex to `(0.0f, 1.5f)`. *Hint: anything outside -1..1 is clipped;
   you should see a flat top.*
3. Draw two separate triangles in one draw call. *Hint: add three more
   vertices to the array; `setVertices` counts them for you, and every three
   vertices form one triangle.*
4. Print the generated shader source. *Hint: `shader.source().source` is a
   `std::string`; print it after `compile()` has run. On Windows you would see
   HLSL instead.*
5. Flip the triangle upside down without touching the vertex array. *Hint: in
   `define()`, `position.x()` and `position.y()` give `Float` handles, and
   `float2(...)` builds a new vector from them.*

## Further reading

- EACP GPU README, [A shader](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#a-shader):
  `define()`, `compile()`, `prepare()` and `draw()` in EACP's own words.
- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has)
  and [What it deliberately refuses](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-it-deliberately-refuses).
- `Apps/GPU/RotatingTriangle` in EACP: the same triangle with per-vertex colours
  and a rotation uniform. `Apps/GPU/Triangle` is the same picture written the
  traditional way, with hand-written Metal/HLSL/GLSL files and a hand-built
  vertex layout, for comparison.
- learnopengl.com, [Hello Triangle](https://learnopengl.com/Getting-started/Hello-Triangle):
  the classic walkthrough of the pipeline, in OpenGL.
- learnopengl.com, [Coordinate Systems](https://learnopengl.com/Getting-started/Coordinate-Systems):
  more on clip space and NDC.

Previous: [01. Hello Window](../01-HelloWindow/README.md) | Next: [03. Interpolation](../03-Interpolation/README.md)
