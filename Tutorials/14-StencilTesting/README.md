# 14. Stencil Testing

Games often outline the object you are pointing at: a bright rim around the
door you can open or the unit you have selected. In this chapter you will put
an outline like that around three cubes standing on a floor. The rim has to
follow each cube's silhouette as the camera moves, without covering the cube
itself, and drawing that is a job for the depth buffer's lesser-known partner,
the **stencil buffer**. You will learn what the stencil buffer holds, how the
**stencil test** decides which fragments survive, what the stencil
**operations** write back on each outcome, and what the **reference value**
and the **masks** are for. Then we use all of that for the outline.

## The stencil buffer

Chapter 08 (Coordinate Systems) gave each pixel a depth value besides its
colour. The stencil buffer gives each pixel one more value: a small unsigned
integer, 8 bits, so 0 to 255. The GPU never computes it from the geometry the
way it computes depth. It is a scratch value that *you* decide how to write
and how to test, a per-pixel note left by one draw for later draws to read.

The name comes from a paper stencil: a sheet with a shape cut out of it, held
over the page so paint only lands inside the shape. The usual pattern is two
steps:

1. Draw something that writes into the stencil buffer, "cutting out" the
   region it covers.
2. Draw something else that only lands where the stencil buffer holds (or does
   not hold) a particular value.

```
   stencil after step 1          what step 2 draws         result
   . . . . . . . .               # # # # # # # #           . . . . . . . .
   . . 1 1 1 1 . .               # # # # # # # #           . . # # # # . .
   . . 1 1 1 1 . .     test:     # # # # # # # #    ->     . . # # # # . .
   . . 1 1 1 1 . .     == 1      # # # # # # # #           . . # # # # . .
   . . . . . . . .               # # # # # # # #           . . . . . . . .
```

Mirrors, portals and windows into another scene work this way (draw the mirror
into the stencil, then draw the reflected world only inside it), and so do the
stencil shadow volumes in EACP's `Apps/GPU/StencilShadows`.

## The stencil test

The stencil test runs for every fragment, right next to the depth test. It
compares a **reference value** you choose against the value already in the
stencil buffer at that pixel:

```
  keep the fragment if   (reference & readMask)  compare  (stored & readMask)
```

`compare` is one of the same eight functions the depth test uses:

| Function | The fragment passes when |
| --- | --- |
| `Never` | never |
| `Less` | reference < stored |
| `LessEqual` | reference <= stored |
| `Equal` | reference == stored |
| `NotEqual` | reference != stored |
| `GreaterEqual` | reference >= stored |
| `Greater` | reference > stored |
| `Always` | always (the default) |

Note the order: it is the *reference* on the left, so `Less` passes where the
reference is less than what is stored.

The **read mask** picks which bits take part in the comparison; both sides are
ANDed with it first. It defaults to all eight bits. Lowering it lets you split
the eight bits into separate fields, for instance bit 0 for "selected" and bit
1 for "inside a mirror", and test each without the other getting in the way.

A fragment that fails the stencil test is thrown away, exactly as one that
fails the depth test is. Its colour is never written.

## Stencil operations

The test decides whether a fragment survives. Separately, you choose what
happens to the *stored* stencil value. There are three outcomes a fragment can
reach, and each gets its own operation:

```
                  stencil test
                 /            \
             fails             passes
               |                  |
         stencilFail         depth test
                             /         \
                         fails          passes
                           |               |
                       depthFail         pass
```

and each operation is one of:

| Operation | Writes |
| --- | --- |
| `Keep` | nothing: the stored value stays (the default) |
| `Zero` | 0 |
| `Replace` | the reference value |
| `IncrementClamp` | stored + 1, stopping at 255 |
| `DecrementClamp` | stored - 1, stopping at 0 |
| `IncrementWrap` | stored + 1, wrapping 255 to 0 |
| `DecrementWrap` | stored - 1, wrapping 0 to 255 |
| `Invert` | every bit flipped |

So the reference value has two jobs: it is what the test compares against,
and it is what `Replace` writes. The `depthFail` outcome is why stencil and
depth are so often used together: "this fragment was hidden behind something"
is information an algorithm can count. Shadow volumes are built on exactly
that.

The **write mask** is the stencil's version of a colour write mask: only the
bits set in it can be changed by any operation. It defaults to all eight bits.
A write mask of `0x00` means the draw cannot change the stencil buffer at all,
whatever its operations say, which is a handy guarantee for a draw that should
only *read* it.

Finally, triangles facing towards the camera and away from it can have
different tests and operations: a **front** set and a **back** set. Most
algorithms set both the same. Shadow volumes are the one that does not, since
they count faces entering a volume up and faces leaving it down.

## Stencil in EACP

Like depth, stencil is an attachment on the view and a setting on every
pipeline. `GPUView::setStencil(true)` gives the view a stencil plane. Both
Metal and Direct3D keep depth and stencil in one combined attachment, so
asking for stencil also turns depth on; you do not need `setDepth(true)` as
well.

On the pipeline, `RenderPipelineDescriptor` has:

- `stencil`, whether the pipeline is built for an attachment with a stencil
  plane. Like `depth`, this has to match the pass. It is about the
  attachment's *format*, not about using the stencil, so **every** pipeline
  drawing into a stencilled view sets it, including ones that never touch the
  stencil.
- `stencilFront` and `stencilBack`, each a `StencilFace` of `compare`,
  `stencilFail`, `depthFail` and `pass`. The defaults are `Always` and `Keep`
  everywhere: test nothing, write nothing.
- `stencilReadMask` and `stencilWriteMask`, shared by both faces, both
  defaulting to `0xff`.

The reference value is not in the pipeline. It is set on the render pass with
`pass.setStencilReference(value)` and holds for every draw after it in that
pass. (Algorithms commonly draw the same geometry with the same pipeline
against different references; keeping the reference out of the pipeline means
not building a new pipeline per value.) The value the stencil plane is cleared
to when the pass starts is `RenderPassDescriptor::clearStencil`, 0 by default.

## Outlining objects

Now the outline. The trick is to draw each object twice:

1. Draw the cubes normally, and let every pixel they cover write 1 into the
   stencil buffer.
2. Draw each cube again, scaled up a little about its own centre, in a flat
   colour, but only where the stencil is **not** 1.

The scaled-up copy covers the original cube plus a thin band around it. The
stencil test rejects the part over the original, since the original wrote 1
there, and what is left is the band: the outline.

```
     step 1: cube writes 1          step 2: bigger cube,           result
                                     kept where stencil != 1
   . . . . . . . . .               . . . . . . . . .            . . . . . . . . .
   . . . . . . . . .               . o o o o o o o .            . o o o o o o o .
   . . 1 1 1 1 1 . .               . o x x x x x o .            . o C C C C C o .
   . . 1 1 1 1 1 . .               . o x x x x x o .            . o C C C C C o .
   . . 1 1 1 1 1 . .               . o x x x x x o .            . o C C C C C o .
   . . . . . . . . .               . o o o o o o o .            . o o o o o o o .
   . . . . . . . . .               . . . . . . . . .            . . . . . . . . .
                                    o kept, x rejected           C cube, o outline
```

Two details make it look right.

**The outline pass ignores depth.** The band sticks out around the cube, and
part of it is often *behind* something in the depth buffer: below the cube
the bigger copy pokes into the floor, and behind a cube it might be hidden by
the cube in front. With the depth test on, the outline would have gaps
wherever that happens. Switching the depth test off for the outline pass
draws the whole band. It also means an outline shows through anything in
front of it that did not write a 1, which for a "selected object" highlight is
usually what you want.

**The floor must not write 1.** Only the outlined objects mark the stencil;
everything else leaves it at 0. Otherwise the outline would also be cut away
over the floor.

Draw order matters too: all the cubes go first, then all the outlines, so
that by the time any outline is drawn every cube has already marked its
pixels. That is also how a cube in front correctly hides the outline of a cube
behind it: the front cube's pixels hold 1, so the back cube's outline is
rejected there.

## Walking through the code

The cube mesh and the lit shader come from chapter 09 (Lighting). The shader
gains one small addition: a checkerboard on the floor, so you can see the
floor as a surface rather than a grey plane. `floor()` of the world xz
position gives the integer cell a fragment is in, and the parity of the sum of
the two cell indices alternates from cell to cell. A uniform `checker` turns
it on (1, for the floor) or off (0, for the cubes):

```cpp
        // One-unit squares on the xz plane: the sum of the two cell indices is
        // even on one colour and odd on the other, and fract(sum / 2) tells
        // the two apart as 0 or 0.5.
        auto cells = floor(fragmentPosition.xz());
        auto odd = fract((cells.x() + cells.y()) * 0.5f) * 2.0f;
        auto pattern = mix(1.0f, 0.55f + 0.45f * odd, checker);
```

The floor itself is one square of two triangles at y = 0, facing up, and the
outline is drawn with chapter 09's flat lamp shader under a new name:

```cpp
// The outline: the same cube, unlit, in one flat colour.
struct FlatShader final : ShaderProgram
{
    FlatShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        setPosition(modelViewProjection * float4(position, 1.0f));
        setFragment(float4(color, 1.0f));
    }

    Uniform<Float4x4> modelViewProjection;
    Uniform<Float3> color;

    EACP_SHADER(modelViewProjection, color)
};
```

None of the stencil work is in the shaders. It is all pipeline state, set up
in the view's constructor, and the three pipelines differ only in that state.
The view asks for a stencil plane, and a `base` descriptor holds what all
three share, including `stencil = true`:

```cpp
        // A stencil plane, which also brings the depth plane with it: the two
        // live together in one attachment.
        setStencil(true);

        // Every pipeline drawing into a pass with a stencil plane must say so,
        // even one that never touches the stencil, because the attachment's
        // format is part of what a pipeline is built for.
        auto base = RenderPipelineDescriptor {};
        base.sampleCount = sampleCount();
        base.depth = true;
        base.stencil = true;
        base.cullMode = CullMode::Back;
```

The floor uses `base` unchanged. Its stencil faces are the defaults, so it is
depth tested as usual and leaves the stencil buffer at 0:

```cpp
        floorShader.setVertices(floorVertices);
        floorShader.setIndices(floorIndices.data(), (int) floorIndices.size());
        floorShader.prepare(base);
```

The cubes always pass the stencil test, and on the `pass` outcome (stencil and
depth both passed, so the fragment is really visible) they `Replace` the stored
value with the reference. The back faces get the same settings; with culling
on they are never drawn, but it is the honest description of the pipeline:

```cpp
        auto marking = base;
        marking.stencilFront.compare = CompareFunction::Always;
        marking.stencilFront.pass = StencilOp::Replace;
        marking.stencilBack = marking.stencilFront;
```

The outline pipeline has the two changes the method needs. The stencil test is
`NotEqual`, so only pixels whose stored value differs from the reference 1
pass. The depth test is switched off by `depthCompare = Always` with
`depthWrite = false`. Notice that `depth` itself stays `true`: that field is
about the attachment, and the pass has a depth plane whether this pipeline
looks at it or not. The zero write mask is the guarantee from earlier:

```cpp
        auto outlining = base;
        outlining.depthCompare = CompareFunction::Always;
        outlining.depthWrite = false;
        outlining.stencilFront.compare = CompareFunction::NotEqual;
        outlining.stencilBack = outlining.stencilFront;
        // Its three ops are all Keep already; a write mask of zero makes "the
        // outline pass never changes the stencil" a guarantee, not a default.
        outlining.stencilWriteMask = 0x00;
```

Each cube's model matrix is chapter 07's `T * R * S`. The blue cube spins, and
the camera orbits the whole scene slowly in `update`:

```cpp
    Mat4 cubeModel(int index) const
    {
        const auto& placement = cubes[index];
        auto angle = placement.angle + (index == 1 ? spin : 0.0f);

        return Mat4::translation(placement.position) * Mat4::rotationY(angle)
               * Mat4::scale(placement.scale);
    }
```

In `render`, the pass is opened with the stencil cleared to 0, then the
reference is set to 1 once for the whole pass. Both stencil pipelines use it:
the cubes write it, the outlines compare against it.

```cpp
        // Zero everywhere to start with: "no cube here".
        auto descriptor = RenderPassDescriptor {};
        descriptor.clearColor = Graphics::Color {0.06f, 0.06f, 0.08f};
        descriptor.clearStencil = 0;

        auto pass = frame.beginPass(descriptor);

        // The value Replace writes and NotEqual compares against.
        pass.setStencilReference(1);
```

Then the three steps, in order. The floor first. Then the cubes: one program,
drawn three times with different uniforms. That works because a draw takes a
copy of the uniform values as they are at that moment, so changing them for
the next cube does not reach back into the previous draw.

```cpp
        // 2. The cubes, marking every pixel they cover with a 1. The same
        // program is drawn three times; each draw takes a copy of the uniforms
        // as they are at that moment.
        cubeShader.checker = 0.0f;

        for (auto i = 0; i < (int) cubes.size(); ++i)
        {
            auto model = cubeModel(i);
            cubeShader.model = model;
            cubeShader.normalMatrix = model.inverted().transposed();
            cubeShader.objectColor = cubes[i].color;
            pass.draw(cubeShader);
        }
```

Finally the outlines. The scale goes on the *right* of the cube's model
matrix, so it is applied first, in the cube's local space, where the cube is
centred on the origin. That grows the cube about its own centre. (On the left
it would scale the cube's *position* too, pushing it away from the world
origin.)

```cpp
        // 3. The outlines: each cube again, scaled up about its own centre, in
        // a flat colour. Inside the cubes the stencil is 1 and the test throws
        // the fragment away; only the rim that sticks out survives.
        outlineShader.color = Vec3 {1.0f, 0.78f, 0.2f};

        for (auto i = 0; i < (int) cubes.size(); ++i)
        {
            auto model = cubeModel(i) * Mat4::scale(outlineScale);
            outlineShader.modelViewProjection = viewProjection * model;
            pass.draw(outlineShader);
        }
```

## What you should see

Three lit cubes, orange, blue and green, stand on a grey checkerboard while the
camera circles them slowly and the blue cube turns. Each has a golden rim that
follows its silhouette exactly as it changes. Where one cube passes in front of
another, the front cube hides the back cube's rim. Along the bottom of each
cube the rim is drawn over the floor even though the scaled-up cube actually
dips below it: that is the depth test being off.

Look closely and you will see the outline is not quite the same thickness
everywhere: it is wider on the bigger cube than on the small one, and it
varies a little around each cube. Scaling an object
by 8% moves its silhouette by 8% of its size *on screen*, which depends on how
big and how close it is. This is the weakness of the scale trick. Moving each
vertex out along its normal instead gives a band of constant thickness in
world space, but a flat-faced cube's faces then separate at the corners; real
engines often do the outline as a screen-space effect on the stencil (or a
mask) after the fact. The scale trick remains the simplest, and for convex,
centred objects it looks fine.

## Try it yourself

1. Turn the depth test back on for the outline pass (delete the
   `depthCompare` and `depthWrite` lines). Where do the rims disappear, and
   why? *Hint: look at the bottom edge of each cube, and at the parts of a
   rim behind another cube.*
2. Swap the outline's `NotEqual` for `Equal`. *Hint: the enlarged copy now
   lands only where a cube already is, so each cube turns flat gold and the
   rim vanishes.*
3. Draw the outlines *between* the cubes instead of after them: cube 0, its
   outline, cube 1, its outline, and so on. What goes wrong where cubes
   overlap on screen? *Hint: a rim drawn before the cube in front of it has
   marked its pixels is not rejected there, and the depth test is off.*
4. Outline only one cube, the "selected" one, and let the user pick it with a
   key or a mouse click. *Hint: the other cubes must not write 1 either, or
   they would cut holes in the selected cube's rim; give them the floor's
   pipeline.*
5. Use the floor's pipeline for the cubes too, but with `pass = Replace` and
   `stencilWriteMask = 0x00`. Does anything get outlined? *Hint: the write
   mask wins over the operation.*
6. Make the rim pulse by animating `outlineScale` (it would need to stop being
   `constexpr`) between 1.03 and 1.12.

## Further reading

- EACP GPU README, [Pipeline state](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#pipeline-state): `depth`, `depthCompare` and `depthWrite` as three separate things, the same split this chapter leans on for the outline pass.
- `StencilFace`, `StencilOp` and `CompareFunction` in EACP's `Lib/eacp/GPU/Pipeline/RenderPipeline.h`, and `RenderPass::setStencilReference` in `Lib/eacp/GPU/Frame/RenderPass.h`: the full stencil API with notes on each field.
- `Apps/GPU/StencilShadows` in EACP: shadow volumes, using separate front and back operations, `depthFail` counting and wrapping increments.
- learnopengl.com, [Stencil testing](https://learnopengl.com/Advanced-OpenGL/Stencil-testing).

Previous: [13. Light Casters](../13-LightCasters/README.md) | Next: [15. Blending](../15-Blending/README.md)
