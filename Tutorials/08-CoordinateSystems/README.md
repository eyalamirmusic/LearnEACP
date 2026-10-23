# 08. Coordinate Systems

In the last chapter a matrix moved a flat quad around the window. Now we go
3D. In this chapter you will draw a spinning cube with a different colour on
each face, seen through a camera with real perspective, so that the far side
of the cube looks smaller than the near side. To get there you will meet the
chain of **coordinate spaces** every 3D renderer passes its vertices through,
the **perspective projection** that makes distance look like distance, and
the two pieces of pipeline state a solid object needs: the **depth buffer**,
so near faces hide far ones, and **back-face culling**, so the faces pointing
away from you are not drawn at all.

## From the model to the screen: five spaces

A vertex's position is only meaningful relative to some origin and some set of
axes, a **coordinate space**. The cube's vertices are written relative to the
cube's own centre, which is convenient for building the cube and useless for
deciding where it appears on screen. So we move each vertex through a chain of
spaces, one matrix per step:

```
  local space ----model----> world space ----view----> view space ----projection----> clip space
  (the cube's     matrix     (where things   matrix    (relative to   matrix          (what setPosition
   own corners)               are in the                the camera)                    writes)
                              scene)
                                                                             |
                                                         divide by w, then   |  done by the GPU
                                                         viewport mapping    v
                                                                        screen pixels
```

- **Local space** (also called object or model space): coordinates relative to
  the object's own origin. Our cube runs from -0.5 to 0.5 on every axis.
- **World space**: one shared space for the whole scene. The **model matrix**
  places, turns and sizes the object in it: exactly the `T * R * S` of the last
  chapter. Two copies of the cube are the same local vertices with two model
  matrices.
- **View space** (camera or eye space): the world as seen from the camera, with
  the camera at the origin. The **view matrix** moves the whole world so that
  the camera ends up there. (Moving the camera left is the same as moving the
  world right; the GPU has no camera, only this matrix.)
- **Clip space**: what the vertex shader writes with `setPosition`, and the
  space you have been working in directly since chapter 02 (Hello Triangle). The **projection
  matrix** takes view space here. Anything that lands outside the visible
  volume is *clipped* (cut away), hence the name.
- **Normalized device coordinates (NDC)** and the screen: the GPU divides clip
  space x, y and z by w (the **perspective divide**), giving x and y in -1..1
  and z in 0..1, then maps x and y to pixels with the viewport.

You write the first three matrices. The GPU does the last step for you. All of
them are multiplied right to left as usual:

```
  clip = projection * view * model * local
```

## Choosing a camera: lookAt

The view matrix is easiest to describe as "a camera at *eye*, looking at
*target*, with *up* pointing roughly upward". `Mat4::lookAt(eye, target, up)`
builds exactly that. It works out the camera's own right, up and backward axes
and builds the matrix that rotates and translates the world into them.

EACP's `Mat4` is **right-handed** with the camera looking down **-z** in view
space, the same convention as OpenGL and learnopengl: x to the right, y up,
and z pointing *out of the screen towards you*.

```
        y
        |
        |
        +------ x         the camera sits at the view-space origin and looks
       /                  along -z, into the screen
      z (towards you)
```

## Perspective projection

Under a **perspective projection**, things further away look smaller and
parallel lines (the edges of the cube) converge. The camera sees a
**frustum**, a pyramid with its tip cut off:

```
     side view
                                    far plane
              near plane          /|
                     /|         /  |
                   /  |       /    |
     eye  <)-----/----|-----/------|------> -z
              ^  \    |     \      |
         fovY |    \  |       \    |
              v      \|         \  |
                                  \|
          <-- near --><------- far ------->
```

`Mat4::perspective(aspect, fovY, near, far)` builds the matrix for it:

- **fovY** is the vertical **field of view**, the angle between the top and the
  bottom of the frustum, in radians. Around 45 degrees looks natural; larger
  is a wide-angle lens, smaller a zoom.
- **aspect** is width / height of the view. The horizontal field of view
  follows from fovY and the aspect, so passing the window's real shape is what
  keeps the cube square, in the same way the aspect correction did in the last
  chapter. We read it from `getLocalBounds()` every frame, so resizing the
  window just works.
- **near** and **far** are the distances to the planes where the frustum starts
  and ends. Anything nearer than `near` or further than `far` is clipped. Keep
  `near` small but not tiny: depth precision is spread very unevenly between
  the two, and a `near` of 0.0001 wastes most of it right in front of the lens.

How does a matrix make far things small, when matrices only multiply and add?
Through w. The projection matrix copies the view-space distance, `-z`, into the
output's w. The GPU then divides x and y by w, so a point twice as far away
ends up half as far from the centre of the screen. This is the second job of
the homogeneous coordinate promised in the last chapter.

It also maps the distance into z. `Mat4::perspective` produces a **depth range
of 0 to 1**: after the divide, the near plane is at z = 0 and the far plane at
z = 1. That is what Metal, D3D12 and Vulkan clip against. (OpenGL uses -1..1,
which is why matrices copied from OpenGL tutorials are subtly different. Use
`Mat4`'s own and you never have to think about it.)

## The depth buffer

Run the program with depth testing off and something looks very wrong: the
cube seems to turn inside out, with back faces painted over front ones.

```
  without depth testing                      with depth testing

  triangles are drawn in index order,        each pixel keeps the nearest
  and each one simply overwrites the         surface: a fragment further away
  pixels it covers. Whichever face           than what is already there is
  comes later in the index buffer wins,      thrown away.
  near or far.
```

The GPU draws triangles one after another, and by default a later triangle's
fragments simply overwrite whatever is in the colour buffer. The fix is the
**depth buffer** (or z-buffer): a second image the size of the window holding
one depth value per pixel. At the start of each frame it is cleared to the far
plane, 1.0. Every fragment's depth (the z from NDC) is compared with the value
stored at its pixel: if it is nearer, it is drawn and its depth is written;
if not, it is discarded. This is the **depth test**, and it makes the drawing
order irrelevant.

In EACP the depth buffer takes **two switches that must agree**:

1. `setDepth(true)` on the `GPUView`, which gives the view's render pass a
   depth attachment (and clears it every frame).
2. `depth = true` in the `RenderPipelineDescriptor` you pass to `prepare`, which
   tells this pipeline to test and write that depth.

A pipeline that says it has depth while the pass has none (or the other way
round) is rejected by the backends, so the two always go together. The default
comparison is `LessEqual` with depth writes on, which is what opaque geometry
wants; you will meet `depthCompare` and `depthWrite` when you draw transparent
things.

## Back-face culling, and which way round is front

A closed object like a cube always has about half of its faces turned away from
the camera. They are hidden behind the front faces, so the depth test would
throw their pixels away anyway, but only *after* rasterizing and shading them.
**Face culling** skips them before that, by looking at each triangle's
**winding**: the order its three vertices go round once projected.

EACP's rule, from its GPU README: **a triangle whose vertices run
counter-clockwise in clip space (y up) is front-facing.** So we list every
face's corners counter-clockwise *as seen from outside the cube*. When a face
turns away from the camera, you are looking at it from behind and the same
three vertices appear clockwise. `CullMode::Back` then discards it.

```
  front face, seen from outside      the same face from behind
     3 ----- 2                         2 ----- 3
     |     / |                         | \     |
     |   /   |      0 -> 1 -> 2        |   \   |    0 -> 1 -> 2
     | /     |      counter-clockwise  |     \ |    clockwise: culled
     0 ----- 1                         1 ----- 0
```

Culling is off by default (`CullMode::None`), because a mesh whose winding is
inconsistent does not draw wrongly when culled, it partly vanishes. Turn it on
for closed meshes you have checked, as here. For one convex object, culling
alone is actually enough to hide the back faces even without a depth buffer,
since no front face ever overlaps another. Two cubes, or one concave mesh,
need the depth buffer; most real renderers use both.

## Walking through the code

Two switches at the top let you see each technique on its own:

```cpp
// Flip these to see what each one is for. With both off, the cube's faces draw
// in index order, so a far face can paint over a near one. Either one alone
// fixes a single convex cube; a real scene needs the depth buffer.
constexpr bool useDepthBuffer = true;
constexpr bool cullBackFaces = true;
```

The vertex now has a `Vec3` position, since we are in 3D. Each face has its
own four vertices with a single colour, which is why the cube needs 24
vertices rather than 8: a vertex carries one colour, and every corner is shared
by three faces of three colours. The indices draw each face as two
counter-clockwise triangles, 36 indices in all:

```cpp
struct Vertex
{
    Vec3 position;
    Vec3 color;
};

namespace
{
// A cube of side 1 centred on the origin, in its own local space. Each face has
// its own four vertices (24 in all) rather than sharing the 8 corners, because
// a vertex carries one colour and each corner belongs to three faces of
// different colours.
//
// Every face lists its corners counter-clockwise as seen from outside the cube,
// looking at that face. After the camera and projection, the faces turned
// towards the viewer are still counter-clockwise, which is what EACP calls
// front-facing.
const Vertex cubeVertices[] = {
    // +X (right): red
    {{0.5f, -0.5f, 0.5f}, {0.90f, 0.30f, 0.30f}},
    {{0.5f, -0.5f, -0.5f}, {0.90f, 0.30f, 0.30f}},
    {{0.5f, 0.5f, -0.5f}, {0.90f, 0.30f, 0.30f}},
    {{0.5f, 0.5f, 0.5f}, {0.90f, 0.30f, 0.30f}},
    // -X (left): cyan
    {{-0.5f, -0.5f, -0.5f}, {0.30f, 0.85f, 0.90f}},
    {{-0.5f, -0.5f, 0.5f}, {0.30f, 0.85f, 0.90f}},
    {{-0.5f, 0.5f, 0.5f}, {0.30f, 0.85f, 0.90f}},
    {{-0.5f, 0.5f, -0.5f}, {0.30f, 0.85f, 0.90f}},
    // +Y (top): green
    {{-0.5f, 0.5f, 0.5f}, {0.35f, 0.85f, 0.35f}},
    {{0.5f, 0.5f, 0.5f}, {0.35f, 0.85f, 0.35f}},
    {{0.5f, 0.5f, -0.5f}, {0.35f, 0.85f, 0.35f}},
    {{-0.5f, 0.5f, -0.5f}, {0.35f, 0.85f, 0.35f}},
    // -Y (bottom): magenta
    {{-0.5f, -0.5f, -0.5f}, {0.85f, 0.35f, 0.85f}},
    {{0.5f, -0.5f, -0.5f}, {0.85f, 0.35f, 0.85f}},
    {{0.5f, -0.5f, 0.5f}, {0.85f, 0.35f, 0.85f}},
    {{-0.5f, -0.5f, 0.5f}, {0.85f, 0.35f, 0.85f}},
    // +Z (front): blue
    {{-0.5f, -0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    {{0.5f, -0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    {{0.5f, 0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    {{-0.5f, 0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    // -Z (back): yellow
    {{0.5f, -0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
    {{-0.5f, -0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
    {{-0.5f, 0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
    {{0.5f, 0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
};

// Two counter-clockwise triangles per face: corners 0-1-2 and 0-2-3.
const std::uint16_t cubeIndices[] = {
    0,  1,  2,  0,  2,  3, // +X
    4,  5,  6,  4,  6,  7, // -X
    8,  9,  10, 8,  10, 11, // +Y
    12, 13, 14, 12, 14, 15, // -Y
    16, 17, 18, 16, 18, 19, // +Z
    20, 21, 22, 20, 22, 23, // -Z
};
```

To check the winding of a face, look at it from outside along its normal. For
the +Z face that is from in front, with x to the right and y up: (-0.5, -0.5),
(0.5, -0.5), (0.5, 0.5), (-0.5, 0.5) goes bottom-left, bottom-right, top-right,
top-left, which is counter-clockwise.

The shader is the smallest 3D shader there is: one matrix takes the local
position all the way to clip space.

```cpp
struct CubeShader final : ShaderProgram
{
    CubeShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);

        // Local space straight to clip space in one multiply: the CPU has
        // already folded model, view and projection into a single matrix.
        setPosition(modelViewProjection * float4(position, 1.0f));

        setFragment(float4(varying(color), 1.0f));
    }

    Uniform<Float4x4> modelViewProjection;

    EACP_SHADER(modelViewProjection)
};
} // namespace
```

We send **one combined matrix** rather than three separate ones. The vertex
shader needs nothing but the final clip position here, so folding
`projection * view * model` on the CPU once per frame saves two
matrix-times-matrix products per vertex and keeps the shader trivial. The next
chapter's lighting needs positions in world space too, and that is the moment
to split the matrices up.

The constructor sets up the two depth switches and the culling mode together.
`RenderPipelineDescriptor` is the long form of `prepare(sampleCount())`: the
same pipeline, with its other fields named instead of left at their defaults.

```cpp
struct CoordinateSystemsView final : GPUView
{
    CoordinateSystemsView()
    {
        // The depth buffer is two switches that must agree: the view allocates
        // the buffer (and clears it every frame), the pipeline tests against it.
        setDepth(useDepthBuffer);

        shader.setVertices(cubeVertices);
        shader.setIndices(cubeIndices);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = useDepthBuffer;
        pipeline.cullMode = cullBackFaces ? CullMode::Back : CullMode::None;
        shader.prepare(pipeline);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        angle += radiansPerSecond * static_cast<float>(time.delta);
    }
```

`render` builds the three matrices and multiplies them right to left:

```cpp
    void render(Frame& frame) override
    {
        // Local -> world: tumble the cube about two axes so every face shows.
        auto model = Mat4::rotationY(angle) * Mat4::rotationX(angle * 0.6f);

        // World -> view: a camera up and in front of the origin, looking at it.
        auto view =
            Mat4::lookAt({0.0f, 1.0f, 2.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});

        // View -> clip: a 45 degree vertical field of view, shaped to the window
        // so the cube stays square, seeing from 0.1 to 100 units away.
        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);

        // Right to left, as always: model first, projection last.
        shader.modelViewProjection = projection * view * model;

        auto pass = frame.beginPass({Graphics::Color {0.10f, 0.10f, 0.12f}});
        pass.draw(shader);
    }
```

The model matrix tumbles the cube about y and x at different rates so every
face comes round. The camera sits at (0, 1, 2.5), slightly above and in front
of the cube, looking at the origin with y up. The projection takes the
window's aspect every frame. Notice that the order of the product,
`projection * view * model`, is the left-to-right reading of the space
diagram backwards: the model matrix touches the vertex first.

## Try it yourself

1. Set `useDepthBuffer` and `cullBackFaces` both to `false` and watch the cube
   turn inside out. Then turn on just one of them, then the other. *Hint: both
   fix a single convex cube, for different reasons.*
2. With depth off and culling off, swap the order of the faces in
   `cubeIndices` and see different faces "win". *Hint: without a depth test the
   last triangle drawn over a pixel is the one you see.*
3. Change `fovY` to 90 degrees and then to 20 degrees, moving the camera's eye
   further away to keep the cube a similar size. *Hint: a narrow field of view
   from far away flattens the perspective, like a telephoto lens.*
4. Reverse one face's winding (swap two of its indices in both triangles) with
   culling on. That face disappears from the outside, and you can see it
   through the cube from the inside. *Hint: a wrongly wound triangle under
   culling does not draw wrongly, it does not draw at all.*
5. Draw a second cube beside the first with a different model matrix and set
   `cullBackFaces = true`, `useDepthBuffer = false`. Place them so they overlap
   on screen. *Hint: culling cannot sort two objects; you need the depth buffer
   back. You will need a second `CubeShader` instance for its own uniform.*

## Further reading

- EACP GPU README, [Pipeline state](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#pipeline-state): depth as an attachment plus `depthCompare` and `depthWrite`, and culling.
- EACP GPU README, [Face culling, and which way round front is](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#face-culling-and-which-way-round-front-is).
- EACP GPU README, [The CPU types a shader value is fed from](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#the-cpu-types-a-shader-value-is-fed-from): `Mat4`'s right-handed, 0..1 depth conventions.
- `Apps/GPU/CubeMap` in EACP builds `lookAt` and `perspective` on the CPU like this chapter; `Apps/GPU/Teapot` builds them inside the shader instead.
- learnopengl.com, [Coordinate Systems](https://learnopengl.com/Getting-started/Coordinate-Systems), [Depth testing](https://learnopengl.com/Advanced-OpenGL/Depth-testing) and [Face culling](https://learnopengl.com/Advanced-OpenGL/Face-culling).

Previous: [07. Transformations](../07-Transformations/README.md) | Next: [09. Lighting](../09-Lighting/README.md)
