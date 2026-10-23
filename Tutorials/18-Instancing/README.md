# 18. Instancing

Several scenes since chapter 11 (Camera) drew their objects the same way: set one
object's uniforms, call `pass.draw`, and repeat for the next object. That is
fine for a dozen crates. It is not fine for a forest, a crowd, a particle
system, or the scene in this chapter: a planet with a ring of **12,000
asteroids** around it. In this chapter you will draw all of those rocks with
**one draw call**, using **instancing**: the GPU draws the same mesh many
times over, and each copy, each *instance*, reads its own position, size,
orientation and colour from a buffer of per-instance data. You will see where
the cost of a draw call actually goes, what the **instance index** is, how
EACP feeds a shader **per-instance attributes**, and how big the difference is
when you switch instancing off.

## The cost of one draw call per object

Here is chapter 11's loop, slightly simplified:

```cpp
for (const auto& object: objects)
{
    shader.model = modelFor(object);
    pass.draw(shader);
}
```

Each `pass.draw` looks cheap, but it is a request to the driver, and the
driver has to do real work for each one on the **CPU**: bind the pipeline and
the vertex buffers, copy this draw's uniform block somewhere the GPU can read
it, check that everything bound is valid, and append the commands to the
command buffer. That work is the same whether the draw has 240 vertices or
240,000. On the **GPU**, a draw of a tiny mesh is a tiny batch of work, too
small to keep thousands of shader cores busy, so the GPU spends much of its
time starting and finishing draws rather than shading.

```
  one draw per rock                        one instanced draw

  CPU: [d][d][d][d][d][d][d]...[d]         CPU: [d]
        12,000 draw calls, each one             one draw call
        binding, uploading, validating
                                           GPU: [=====all rocks=====]
  GPU:   [r] [r] [r] [r] [r] ... [r]             one big batch, every
        tiny batches, waiting on the CPU         core busy
```

With a few objects none of this matters. With thousands, the CPU spends the
whole frame talking to the driver, and the frame rate is set by the number of
draw calls, not by the number of triangles. The fix is to say, once: "draw
this mesh 12,000 times, and here is what differs between the copies".

## Instances and the instance index

An **instanced draw** takes one more number than an ordinary draw: the
**instance count**. The GPU then runs the whole mesh that many times. Every
vertex shader invocation gets two indices rather than one:

- the **vertex index**: which vertex of the mesh this is, 0 to 239 for our rock;
- the **instance index**: which copy of the mesh this is, 0 to 11,999.

In GLSL these are `gl_VertexID` and `gl_InstanceID`; in Metal they are
`[[vertex_id]]` and `[[instance_id]]`. If every instance ran exactly the same
vertex shader on exactly the same data, all 12,000 rocks would land on top of
each other. The instance index is what lets each copy be different: it picks
that copy's record out of a buffer of per-instance data.

## Per-vertex and per-instance data

There are two common ways for a shader to get at that per-instance data:

1. Read it from a **storage buffer**, indexing it yourself with the instance
   index, as chapter 13 (Light Casters) read its lights.
2. Declare it as **per-instance vertex attributes**. A vertex buffer has a
   **step rate**: an ordinary one advances one record per *vertex*; an
   instanced one advances one record per *instance*. The GPU does the indexing
   and hands the shader the right record as if it were a normal attribute.

EACP's EDSL takes the second road, and it is the idiomatic one whenever the
data is exactly one record per instance. The rock shader reads from two
vertex buffers at once:

```
                    slot 0, per vertex                 slot 1, per instance
                    (the rock mesh, 240 vertices)      (the Rock buffer, 12,000 records)

  instance 0:       v0 v1 v2 ... v239                  rock 0  (matrix, tint)
  instance 1:       v0 v1 v2 ... v239                  rock 1
  instance 2:       v0 v1 v2 ... v239                  rock 2
     ...
  instance 11999:   v0 v1 v2 ... v239                  rock 11999
```

Slot 0 is the one every chapter so far has used, filled by `setVertices` and
read with `vertexInput`. The new pieces are three calls:

- `instanceInput(&Struct::field, slot)` in `define()`, the per-instance sibling
  of `vertexInput`. It declares that `slot` is a per-instance buffer of
  `Struct` records and returns a handle to one of the fields.
- `setInstances(slot, data, count)` on the CPU, the sibling of `setVertices`:
  it uploads the records for that slot.
- `pass.drawInstanced(shader, instanceCount)` instead of `pass.draw(shader)`.

You never touch the instance index yourself in this scheme: the GPU uses it to
fetch the record, and your shader just sees the record's fields. The index is
still there in one place, though: `drawInstanced` takes an optional third
argument, `firstInstance`, the index of the first record to use. We will use it
at the end of the chapter.

## A matrix, four columns at a time

learnopengl's version of this chapter sends each asteroid's model matrix as a
per-instance attribute, and so do we. There is a catch: a vertex attribute is
at most **four floats** wide, on every graphics API. A 4x4 matrix is sixteen,
so it travels as four attributes, its four columns, and the shader puts it
back together:

```cpp
// Per instance: one record for every rock. A vertex attribute is at most four
// floats, so the 4x4 model matrix travels as its four columns and is put back
// together in the shader.
struct Rock
{
    Vec4 model0;
    Vec4 model1;
    Vec4 model2;
    Vec4 model3;
    Vec3 tint;
};
```

`Mat4` is column-major, so `model.column(0)` to `model.column(3)` on the CPU
and `float4x4(c0, c1, c2, c3)` in the EDSL are exact opposites. A record is 76
bytes, so the whole field is about 900 KB, uploaded once.

A full matrix per instance is the most general choice. It is not the most
compact: a position, a uniform scale and a rotation would fit in 8 floats, and
the shader could rebuild the matrix from them. That trade, memory against
shader arithmetic, is one of the exercises.

## Building the field

The rock mesh is an icosahedron (twenty triangles) with each triangle split
into four, and every corner pushed in or out by a small hash of its direction.
`makeRock` gives each triangle its own three vertices sharing the face normal,
for a chipped, faceted look, and flips any triangle whose normal ended up
pointing inwards so back-face culling stays correct. It is 240 vertices; the
details are in `Main.cpp`. Every rock in the scene is this same mesh. What
makes them look different is entirely the per-instance data.

The field itself is generated once, on the CPU, with a seeded random number
generator. Each rock gets a random angle around the ring, a distance from the
centre and a height, a size, a random orientation built from three rotations,
and a slightly different shade of grey-brown:

```cpp
std::vector<Rock> makeRockField()
{
    auto random = std::mt19937 {42};
    auto unit = std::uniform_real_distribution<float> {0.0f, 1.0f};
    auto signedUnit = std::uniform_real_distribution<float> {-1.0f, 1.0f};

    auto rocks = std::vector<Rock> {};
    rocks.reserve(rockCount);

    for (auto i = 0; i < rockCount; ++i)
    {
        auto angle = 2.0f * 3.14159265f * unit(random);

        // Summing two random numbers bunches the rocks towards the middle of
        // the ring and thins them out at its edges.
        auto spread = 0.5f * (signedUnit(random) + signedUnit(random));
        auto radius = ringRadius + spread * ringWidth;
        auto height = 0.12f * signedUnit(random) * (1.0f - std::abs(spread));

        auto position =
            Vec3 {radius * std::cos(angle), height, radius * std::sin(angle)};

        // Mostly small rocks, with the occasional big one.
        auto size = 0.012f + 0.03f * std::pow(unit(random), 3.0f);

        auto model = Mat4::translation(position)
                     * Mat4::rotationY(6.2832f * unit(random))
                     * Mat4::rotationX(6.2832f * unit(random))
                     * Mat4::rotationZ(6.2832f * unit(random)) * Mat4::scale(size);

        auto shade = 0.45f + 0.4f * unit(random);
        auto warmth = 0.1f * unit(random);

        rocks.push_back({model.column(0),
                         model.column(1),
                         model.column(2),
                         model.column(3),
                         {shade + warmth, shade, shade - warmth}});
    }

    return rocks;
}
```

The model matrix is chapter 07's `T * R * S` again, with three random rotations
in the middle. Two small tricks shape the ring. Adding two uniform random
numbers gives a triangle-shaped distribution, so rocks crowd the middle of the
ring and thin out at its edges instead of stopping at a hard border. And
cubing a random number in 0..1 pushes most values towards zero, so most rocks
are pebbles and only a few are boulders.

The fixed seed, 42, means the field is the same on every run, which makes
screenshots and timings comparable.

## Walking through the code

The rock shader is chapter 09's lit shader with its `model` uniform replaced
by per-instance attributes:

```cpp
struct RockShader final : ShaderProgram
{
    RockShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto model = float4x4(instanceInput(&Rock::model0, 1),
                              instanceInput(&Rock::model1, 1),
                              instanceInput(&Rock::model2, 1),
                              instanceInput(&Rock::model3, 1));
        auto tint = instanceInput(&Rock::tint, 1);

        // The rock's own matrix places it in the ring; the shared ring
        // rotation, one uniform, then turns the whole field at once.
        auto world = ringRotation * model;
        auto worldPosition = world * float4(position, 1.0f);

        // Rotations and uniform scales only, so the model matrix itself can
        // transform the normal (chapter 09's first option).
        auto worldNormal = (world * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * worldPosition);

        auto n = normalize(varying(worldNormal));
        auto diffuse = max(dot(n, sunDirection), 0.0f);
        auto color = varying(tint) * (0.08f + 0.92f * diffuse);
        setFragment(float4(color, 1.0f));
    }

    Uniform<Float4x4> ringRotation;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> sunDirection;

    EACP_SHADER(ringRotation, viewProjection, sunDirection)
};
```

`vertexInput` reads slot 0, which changes every vertex. `instanceInput(..., 1)`
reads slot 1, which changes every instance: all 240 vertices of rock 7 see
rock 7's matrix and tint. Every `instanceInput` here names slot 1 and the same
`Rock` struct, so they are fields of one buffer. (Fields could also be split
across several slots, one buffer each, as EACP's `Apps/GPU/Instancing` does
with a transform stream and a colour stream.) The tint is passed to the
fragment stage with `varying`, like any other value computed in the vertex
stage.

Look at what is *not* per instance. The ring turns slowly, but no rock's
matrix ever changes: the turning is one uniform, `ringRotation`, applied on top
of every rock's own matrix. Animating 12,000 objects costs one 64-byte uniform
per frame instead of re-uploading 900 KB. Whenever you can describe a motion
as something shared, put it in a uniform; per-instance data that has to
change every frame is better computed on the GPU, as `Apps/GPU/ComputeParticles`
does with a compute kernel writing the instance buffer directly.

Because every rock's matrix is rotations, a translation and a uniform scale,
its normals can go through the same matrix, the first of chapter 09's two
options. A non-uniform scale per rock would need a per-instance normal matrix
too, which means another three or four columns per record.

The planet is an ordinary draw. Its sphere is built from rings of latitude,
with indices, and its colour is painted procedurally in the fragment shader,
much like chapter 05 (Fragment Playground) painted a full-screen quad: two sine
waves of latitude, bent by a slower wave around the planet, blend a sandy and
a rusty colour into bands. Because the pattern is computed from the *local*
position, it turns with the planet.

```cpp
        // Bands of latitude, bent by a slow wave around the planet.
        auto swirl = sin(p.x() * 2.0f + p.z() * 1.5f) * 0.12f;
        auto bands = sin((p.y() + swirl) * 16.0f) * 0.5f + 0.5f;
        auto fine = sin((p.y() - swirl * 0.5f) * 47.0f) * 0.5f + 0.5f;

        auto sand = float3(constant(0.86f), 0.68f, 0.45f);
        auto rust = float3(constant(0.58f), 0.29f, 0.17f);
        auto surface = mix(rust, sand, bands * 0.75f + fine * 0.25f);
```

The view sets up both programs once. The new line is `setInstances`, which
uploads the 12,000 records into slot 1, next to the rock mesh in slot 0:

```cpp
        // The rock mesh goes in once as ordinary vertices, and the 12,000
        // records go in once as per-instance data in slot 1. Neither changes
        // again: animating the ring is a single uniform.
        rockShader.setVertices(rockMesh.data(), (int) rockMesh.size());
        rockShader.setInstances(1, rocks.data(), (int) rocks.size());
        rockShader.prepare(pipeline);
```

The record type you pass must be exactly the struct `instanceInput` was given:
a debug build asserts that its size matches the stride the shader declared for that slot.

And then the draw. The planet is one `pass.draw`; the whole asteroid field is
one `pass.drawInstanced`:

```cpp
        auto pass = frame.beginPass({Graphics::Color {0.01f, 0.01f, 0.025f}});
        pass.draw(planetShader);

        if (instanced)
        {
            // One draw call: the GPU runs the rock mesh rockCount times, with
            // the instance index stepping through the Rock buffer.
            pass.drawInstanced(rockShader, rockCount);
        }
        else
        {
            // The way chapters 11, 13 and 14 drew their objects: one draw call per
            // rock. Each call draws a single instance, starting at rock i, so
            // the picture is identical; only the number of calls changes.
            for (auto i = 0; i < rockCount; ++i)
                pass.drawInstanced(rockShader, 1, i);
        }
```

The `else` branch is there for comparison. It draws the field the old way, one
draw call per rock, but it does so with the same shader and the same buffer:
each call draws **one** instance, starting at instance `i`. That is
`firstInstance` at work: it offsets where the instance index starts counting,
so the call for rock `i` reads record `i`. The picture is identical in both
modes; the only thing that changes is 2 draw calls against 12,001. Every one
of those calls binds the pipeline and buffers again and records its own copy
of the uniform block, exactly as chapter 11's `drawCube` did.

## Timing it

The view measures two numbers and prints their averages to the console every
two seconds:

- the **frame time**, the time between one frame and the next, from the
  `FrameTime` that `update` receives;
- the **render() CPU time**, how long `render()` itself took to record the
  frame, from `std::chrono::steady_clock` around its body.

Press **Space** to switch between the two modes. On the machine this chapter
was written on, in a debug build, the output looks like this:

```
instanced        2 draw calls   frame  16.67 ms   render() CPU   0.03 ms
one by one   12001 draw calls   frame  27.05 ms   render() CPU  26.28 ms
```

Your numbers will differ, but the shape will not. With instancing, recording
the frame takes a few hundredths of a millisecond, and the frame time is just
the display's refresh interval: the program is idle most of the frame. One
draw per rock costs hundreds of times more CPU time, all of it spent issuing
draw calls, and the frame time follows it up: the display waits for a frame
the CPU has not finished describing. The GPU draws exactly the same triangles
in both cases. (A release build shrinks the per-call cost, but not the number
of calls; push `rockCount` up and the one-by-one mode falls behind again.)

Note what `render()`'s timer does *not* measure: the GPU's own time. When the
GPU, rather than the CPU, is the bottleneck, the frame time grows while the
CPU time stays small. Here, both modes give the GPU the same work, and that
work is small, so the gap is all CPU.

## What you should see

A banded, sandy planet, lit from the left, inside a broad ring of grey-brown
rocks seen from slightly above. The rocks are of many sizes, from specks to
small boulders, densest in the middle of the ring and thinning at its edges,
and each one catches the light on its own facets. The ring turns slowly around
the planet, which spins a little faster. The console prints a timing line
every two seconds; Space switches modes, and the picture does not change at
all when you do.

## Try it yourself

1. Change `rockCount` to 100,000 and try both modes. *Hint: the instanced mode
   barely notices; the one-by-one mode will crawl. At some count even the
   instanced mode slows down: now the GPU is the bottleneck, and the CPU time
   still stays tiny.*
2. Draw only part of the field: pass a smaller count to `drawInstanced`, or
   use `firstInstance` to draw rocks 3,000 to 5,999. *Hint: the rocks were
   placed at random angles, so any contiguous range of records is scattered
   all round the ring. Sort the records by angle in `makeRockField` and the
   same range becomes an arc.*
3. Make each rock tumble on its own. Add a per-instance spin speed to `Rock`
   and a `time` uniform, and rotate the vertex position about the rock's own
   centre before applying its matrix. *Hint: the matrices never change; the
   tumbling is computed in the vertex shader from `time * spin`, which keeps
   the CPU out of it.*
4. Make the record smaller. Replace the four matrix columns with a `Vec4`
   holding position and scale and a `Vec3` of three rotation angles, and
   rebuild the matrix in the shader with `sin` and `cos`. *Hint: that is 28
   bytes instead of 76 per rock. Is there a visible difference in speed at
   12,000 rocks? At a million?*
5. Give each rock a non-uniform scale, say `Mat4::scale({1.5f, 0.6f, 1.0f})`
   multiplied in before `Mat4::scale(size)`, and look closely at the lighting.
   *Hint: re-read chapter 09's "Transforming normals". The normal matrix has
   to travel per instance too, or be avoided.*
6. Add a second ring, tilted and further out, with smaller, bluer rocks. Do it
   without a second draw call. *Hint: it is just more records in the same
   buffer.*

## Further reading

- EACP GPU README, [Waiting for one command buffer](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#waiting-for-one-command-buffer): `setInstanceBuffer`, a compute kernel's output read directly as a per-instance stream.
- EACP GPU README, [Buffers a shader stage reads](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#buffers-a-shader-stage-reads): the storage-buffer alternative, and when `instanceInput` is the better fit.
- `Apps/GPU/Instancing` in EACP: the same triangles drawn without instancing, with `drawInstanced` from two per-instance slots, and one row at a time with `firstInstance`.
- `Apps/GPU/ComputeParticles` in EACP: per-instance data computed on the GPU every frame and drawn instanced.
- learnopengl.com, [Instancing](https://learnopengl.com/Advanced-OpenGL/Instancing), whose asteroid field this chapter follows.
- Apple, [Metal Best Practices Guide: Draw Call Performance](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/), on why the number of draw calls matters.

Previous: [17. Cubemaps](../17-Cubemaps/README.md) | Next: [19. Anti-Aliasing](../19-AntiAliasing/README.md)
