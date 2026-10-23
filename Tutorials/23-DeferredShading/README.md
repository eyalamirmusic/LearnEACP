# 23. Deferred Shading

Every lit scene so far has been drawn the same way. Each object goes through
a shader that works out its surface and lights it on the spot, one light after
another, and writes the finished colour. This is called **forward rendering**,
and with one light (chapter 09) or six (chapter 13) it is the right choice.
Now try a hundred lights. In this chapter you will light a 9x9 grid of cubes
with 96 small coloured lights that drift between them, using **deferred
shading**: the scene is drawn first with no lighting at all, into a set of
textures called the **G-buffer**, and then a single full-screen pass lights
every pixel once from what those textures hold. You will also see how to skip
lights that are too far away to matter, how to draw unlit light markers on top
with correct depth, and what the technique costs you in exchange.

## The trouble with many lights

In a forward renderer the lighting loop lives in the object's fragment
shader. Its cost is roughly:

```
   forward:   fragments shaded  x  lights
```

and "fragments shaded" is larger than the number of pixels on screen. The
depth test can only reject a fragment against what is already drawn, so where
a far cube is drawn before a near one, the far one is lit in full and then
overwritten. In a dense
scene each pixel may be shaded two, three or more times, and each of those
times loops over every light. With 96 lights and a cube grid seen from above,
most of that work produces colours nobody sees.

## The idea: decide visibility first, light afterwards

Lighting a pixel needs only a handful of facts about the surface at that
pixel: where it is, which way it faces, what colour it is and how shiny. So
split the frame in two:

1. **Geometry pass.** Draw every object, but instead of lighting it, write
   those facts into textures the size of the screen. The depth test does its
   usual job, so when the pass ends each texel holds the facts of the *nearest*
   surface only.
2. **Lighting pass.** Draw one rectangle over the whole screen. For each pixel,
   read the facts back and loop over the lights.

```
   geometry pass (no lighting)                lighting pass (one quad)

   cubes + floor --> [ position ]  --+
                     [ normal   ]  --+--> for each pixel:     --> lit image
                     [ albedo   ]  --+      for each light:
                     [ depth    ]             add its light
```

Now the cost is:

```
   deferred:  fragments drawn  x  (cheap: write a few values)
            + pixels           x  lights
```

Each pixel is lit exactly once, however many surfaces were drawn over it. The
geometry and the lighting no longer multiply together: you can add objects
without the light loop getting longer, and add lights without every object
paying for them.

## The G-buffer

The textures the geometry pass writes are together called the **G-buffer**
(geometry buffer). What goes in it is a design decision; this chapter stores
the same things chapter 09's Phong shader used:

| Attachment       | Holds                                   | Format        |
|------------------|-----------------------------------------|---------------|
| position         | world-space position; w = 1 if covered  | `RGBA32Float` |
| normal           | world-space unit normal                 | `RGBA16Float` |
| albedo/specular  | surface colour, specular strength in w  | `RGBA8Unorm`  |
| depth            | the depth buffer, kept for later        | `D32F`        |

The formats are chosen per attachment. A position is a world coordinate
several units across, which needs real floating point: half floats would
round a point 8 units away to the nearest 0.004 or so. A normal is between
-1 and 1, which a half float holds easily (an 8-bit unsigned format could not
hold the negative half at all). An albedo is an ordinary colour, so 8 bits a
channel is enough, and the one channel left over is a good home for the
specular strength.

The w of the position is a flag. The texture is cleared to all zeros, and
every surface writes w = 1, so a texel still holding w = 0 after the
geometry pass is sky: there is nothing there to light.

This layout is the plainest one that works. Production renderers squeeze much
harder, because the G-buffer is read and written at full resolution every
frame and its size is memory bandwidth. The usual tricks are to store no
position at all and rebuild it from the depth buffer and the pixel's screen
position, and to pack a normal into two numbers instead of three. One of the
exercises reconstructs the position from depth.

## Filling several textures at once

The geometry pass has to write three colours per fragment, one into each
attachment. Graphics APIs support exactly that: a render pass can have
several colour attachments, called **multiple render targets** (MRT), and the
fragment shader returns one value per attachment. In OpenGL you attach
several textures to a framebuffer and call `glDrawBuffers`; in EACP it is
`Frame::beginMultiTargetPass`, which takes up to eight targets, each with its
own clear colour:

<!-- snippet: illustrative -->
```cpp
const ColorTarget targets[] = {
    {&position, {0.f, 0.f, 0.f, 0.f}},
    {&normal, {0.f, 0.f, 0.f, 0.f}},
    {&albedoSpecular, {0.f, 0.f, 0.f, 0.f}},
};

auto pass = frame.beginMultiTargetPass(targets);
```

The pipeline then names a format for each attachment, the first in
`colorFormat` as always and the rest in `extraColorTargets`.

There is one catch, and it is EACP's rather than the GPU's: the shader EDSL
emits **one** fragment output today. `setFragment` takes one `Float4`, and
there is no way yet to say "this value goes to attachment 1". A fragment
shader with several outputs is possible in EACP, but it has to be written by
hand in each backend's shading language (MSL, HLSL and GLSL) and fed in as a
`ShaderSource`, which is a long way outside this series.

So this chapter fills the G-buffer the portable way: **one pass per
attachment**. The same scene is drawn three times, each time by a variant of
the geometry shader that writes one of the three values. That costs three
trips through the vertex stage instead of one, and the program is written
so that switching to a single MRT pass, once the EDSL supports it, changes
only how the G-buffer is written and nothing that reads it. The point of the
technique survives intact: the geometry passes do no lighting at all, and the
expensive part, the light loop, still runs once per pixel.

The three passes all test against one depth buffer, and each clears it on the
way in. Why not lay depth down once and let the other two passes test with
"equal"? Because it is not reliable: the three variants are different
programs, and the shader compiler is free to evaluate even an identical
expression in a slightly different order in each, so the same triangle can
land a hair nearer or further. Try it and you get a hatched pattern on some
faces where the later passes lost the comparison.

## The lighting pass

The lighting pass is a full-screen quad, like chapter 16's post-processing
pass, and its fragment shader is chapter 09's lighting, in the Blinn-Phong form
from that chapter's exercises, with its inputs swapped: where the forward shader got its position and normal from the
vertex stage and its colour from uniforms, this one samples them from the
G-buffer at the pixel's own UV.

The lights are 96 records in a storage buffer, as chapter 13's four point
lights were. Chapter 13 looped over its lights with a C++ `for`, which runs
while `define()` records the graph and leaves four unrolled copies of the
lighting code in the shader. That is fine for four lights and silly for 96.
Here the loop is the EDSL's own `loop`, over a mutable `var`, against a
`Uniform<UInt>` count: a real loop in the generated shader, run on the GPU,
whose length can change from frame to frame without recompiling anything.

The lights move every frame, so the buffer's contents change every frame too.
Overwriting a buffer the GPU may still be reading for the previous frame is a
race. EACP's `StreamingBuffers` is made for exactly this: each `write` copies
the data into a slice of memory no frame still in flight is using, and returns
that slice as a `BufferRange` to bind.

## Light volumes

Deferred shading takes the light loop out of the overdraw, but every pixel
still visits every light. A point light's attenuation (chapter 13) never
quite reaches zero, yet past some distance its contribution is too small to
see. So give every light a **radius** and skip it for pixels outside:

```
   attenuated brightness = brightest channel / (1 + linear*d + quadratic*d²)
```

Set that equal to the smallest step an 8-bit display can show, about 5/256,
and solve the quadratic for `d`:

```
   quadratic*d² + linear*d + (1 - brightest * 256/5) = 0

         -linear + sqrt(linear² - 4*quadratic*(1 - brightest * 256/5))
   d  =  --------------------------------------------------------------
                               2 * quadratic
```

The CPU computes this once per light and sends it in the light's record. With
this chapter's attenuation (linear 0.7, quadratic 1.8) and lights of
brightness 1.6, each light reaches about 6.5 units, so a pixel on one side of
the grid skips most of the lights on the other.

The sphere a radius describes is the light's **volume**. Testing it inside
the loop, as this chapter does, saves the lighting maths but still reads
every record for every pixel. Real deferred renderers go further and never
visit far-away lights at all: they draw each light's volume as an actual
sphere mesh and light only the pixels it covers, or split the screen into
tiles and build a list of the lights touching each tile. Both build on exactly
the G-buffer you have here.

A GPU branch also costs less than you might hope when neighbouring pixels
disagree. Pixels are shaded in groups that share an instruction stream, and
if some pixels in a group are inside a light's radius and some are not, the
whole group steps through the lighting code. The test pays off because a
light is small on screen, so most groups agree.

## Mixing in forward rendering

Not everything suits deferred shading. The light markers here are small
unlit cubes, and running them through the G-buffer would mean inventing a
G-buffer value that says "do not light me". Transparent objects are worse:
a G-buffer texel holds one surface, so there is nowhere to keep the surface
behind the glass. The usual answer is to draw those things **forward, after
the lighting pass**, into the lit image.

For that to look right they need depth testing against the deferred scene, so
a marker behind a cube is hidden. In OpenGL this is the step where you blit
the G-buffer's depth into the default framebuffer. In EACP no copy is needed,
because a pass can borrow another target's depth buffer:

- The lit image is a render target with a depth buffer of its own.
- The geometry passes draw into the G-buffer textures but name the lit target
  in `RenderPassDescriptor::depthTarget`, so they test against and write *its*
  depth buffer, and keep it (`DepthAction::Keep`).
- The lighting pass draws into the lit target with `DepthAction::Resume`,
  which loads the depth the geometry left instead of clearing it. The
  full-screen quad ignores depth (compare `Always`, no write); the markers
  after it test against it as usual.

A final pass then draws the lit image into the window the way chapter 22
(HDR and Bloom) does: its exposure curve brings the many overlapping lights
into the displayable range, and `pow(mapped, 1 / 2.2)` gamma-encodes the
linear result, because the window's drawable is not an sRGB format and will
not do that for us. The G-buffer insets are shown raw, without either step,
since they are data rather than light.

## Walking through the code

The geometry shader takes the attachment it writes as a constructor argument,
the way chapter 16's post shader took its effect, and `define()` records only
that output. The vertex stage is the usual world-space transform:

```cpp
struct GeometryShader final : ShaderProgram
{
    explicit GeometryShader(Attachment attachmentToWrite)
        : attachment(attachmentToWrite)
    {
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * float4(worldPosition, 1.0f));

        auto fragmentPosition = varying(worldPosition);

        switch (attachment)
        {
            case Attachment::Position:
                setFragment(float4(fragmentPosition, 1.0f));
                break;

            case Attachment::Normal:
                setFragment(float4(normalize(varying(worldNormal)), 0.0f));
                break;

            case Attachment::AlbedoSpecular:
            {
                // The checkerboard from chapter 16, so the floor has detail.
                auto square = mod(
                    floor(fragmentPosition.x()) + floor(fragmentPosition.z()), 2.0f);
                auto albedo = objectColor * (1.0f - checker * 0.4f * square);
                setFragment(float4(albedo, specular));
                break;
            }
        }
    }
```

Notice what is missing: no light, no camera position, no `dot` or `pow`. The
position variant writes w = 1, the flag the lighting pass tests.

The lighting shader samples the three textures with nearest filtering. Pixel
and texel line up one to one, so nearest is exact, and it matters for more
than speed: a linear filter at a cube's silhouette would average a point on
the cube with a point on the floor behind it and invent a position in
mid-air.

```cpp
        auto stored = sample(gPosition, uv);
        auto surfacePosition = stored.xyz();
        auto n = normalize(sample(gNormal, uv).xyz());
        auto albedoSpecular = sample(gAlbedoSpecular, uv);
        auto albedo = albedoSpecular.xyz();
        auto specularStrength = albedoSpecular.w();

        auto v = normalize(cameraPosition - surfacePosition);

        auto color = var(albedo * 0.005f);
```

Then the loop. `i` is an EDSL variable; `i < lightCount` compares it with a
uniform, so the condition is re-tested on the GPU every time round. Each
light is two `Float4`s, read with `read4`: position and radius, then colour.
`ifThen` is the light volume test, and `color = color.get() + ...` adds to
the running total:

```cpp
        auto i = var(0u);

        loop(i < lightCount,
             [&]
             {
                 auto record = lights.read4(i.get() * 2u);
                 auto lightColor = lights.read4(i.get() * 2u + 1u).xyz();

                 auto toLight = record.xyz() - surfacePosition;
                 auto distance = length(toLight);

                 // The light volume test: beyond its radius a light's
                 // contribution is too small to see, so skip the maths.
                 ifThen(distance < record.w(),
                        [&]
                        {
                            auto l = toLight / distance;
                            auto h = normalize(l + v);

                            auto diffuse = max(dot(n, l), 0.0f);
                            auto specular =
                                specularStrength * pow(max(dot(n, h), 0.0f), 32.0f);
                            auto attenuation =
                                1.0f
                                / (1.0f + attenuationLinear * distance
                                   + attenuationQuadratic * distance * distance);

                            color = color.get()
                                    + (diffuse * albedo + specular) * lightColor
                                          * attenuation;
                        });

                 i += 1u;
             });

        auto lit = select(stored.w() > 0.5f, color.get(), background);
        setFragment(float4(lit, 1.0f));
```

The `i += 1u` sits outside the `ifThen`, at the end of the body. If you are
tempted to skip a light with `continueLoop()` instead, remember that it jumps
straight back to the condition, past the increment, and the loop never ends.

The light record on the CPU matches what `read4` expects: eight floats, the
radius tucked into the first `Float4`'s w and the second's w left as padding.

```cpp
struct PointLight
{
    Vec3 position;
    float radius;
    Vec3 color;
    float padding;
};
```

and the radius comes straight from the formula in "Light volumes":

```cpp
float lightRadius(const Vec3& color)
{
    auto brightest = std::max({color.x, color.y, color.z});
    auto c = 1.0f - brightest * (256.0f / 5.0f);
    auto l = attenuationLinear;
    auto q = attenuationQuadratic;

    return (-l + std::sqrt(l * l - 4.0f * q * c)) / (2.0f * q);
}
```

The targets are made in `resizeTargets`, recreated whenever the window's
size changes as in chapter 16. The three G-buffer textures have no depth;
the lit target has the one depth buffer everything shares:

```cpp
        for (auto i = 0; i < attachmentCount; ++i)
        {
            descriptor.format = formatOf((Attachment) i);
            gBuffer[i].emplace(Device::shared().makeTexture(descriptor));
        }

        // The lit target owns the one depth buffer every pass shares.
        descriptor.format = litFormat;
        descriptor.depth = true;
        lit.emplace(Device::shared().makeTexture(descriptor));
```

`render` is five passes. The first three are the geometry passes, each into
one G-buffer texture with the lit target's depth buffer borrowed through
`depthTarget`:

```cpp
        for (auto i = 0; i < attachmentCount; ++i)
        {
            auto descriptor = RenderPassDescriptor {};
            descriptor.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            descriptor.depthAction = DepthAction::Keep;
            descriptor.depthTarget = {&*lit};

            auto pass = frame.beginPass(*gBuffer[i], descriptor);
            drawScene(pass, geometryShaders[i], viewProjection);
        }
```

The fourth is the lighting pass into the lit target, resuming its depth. The
lights go into the streaming buffer, the quad lights the screen, and the
markers are drawn forward on top:

```cpp
        {
            auto descriptor = RenderPassDescriptor {};
            descriptor.depthAction = DepthAction::Resume;

            auto pass = frame.beginPass(*lit, descriptor);

            lightingShader.lights = lightStream.write(lights.data(), sizeof(lights));
            lightingShader.lightCount = (unsigned) lightCount;
            lightingShader.cameraPosition = cameraPosition;
            lightingShader.background = Vec3 {0.001f, 0.001f, 0.0015f};
            pass.draw(lightingShader);

            for (const auto& light: lights)
            {
                auto model = Mat4::translation(light.position) * Mat4::scale(0.12f);
                lampShader.modelViewProjection = viewProjection * model;
                lampShader.color = light.color;
                pass.draw(lampShader);
            }
        }
```

For that pass to work, the two pipelines drawing into it must agree that it
has a depth buffer, and differ in what they do with it:

```cpp
        auto lightingPipeline = RenderPipelineDescriptor {};
        lightingPipeline.sampleCount = 1;
        lightingPipeline.depth = true;
        lightingPipeline.depthCompare = DepthCompare::Always;
        lightingPipeline.depthWrite = false;
        lightingPipeline.colorFormat = pixelFormatFor(litFormat);
```

The marker pipeline keeps the defaults (`LessEqual`, writing), so it hides
markers behind cubes.

The last pass draws into the window with `DisplayShader`, one program per
thing to show. Each takes a rectangle in clip space as a uniform and maps the
unit quad onto it, so the same quad draws the full-screen image and the four
insets: position (scaled from world units into 0..1), normal (`n * 0.5 + 0.5`),
albedo, and specular as grey. Looking at the G-buffer like this is the first
thing to do when a deferred renderer's lighting looks wrong.

## What you should see

A field of pale cubes of four heights on a checkered floor, seen from a camera
that slowly circles it. Ninety-six small coloured markers drift between and above
the cubes, each in its own pool of coloured light that tints the floor and the
faces around it; where pools overlap the colours add. Cubes in front hide the
markers behind them.

Along the bottom are four insets showing the G-buffer: the world position as a
smooth gradient (red grows along x, blue along z), the
normals as flat pastel faces (the floor's straight-up normal is green), the
albedo with the floor's checkerboard, and the specular strength in four
shades of grey. Click to hide or show them.

## What deferred shading costs you

- **Memory and bandwidth.** Three full-screen textures plus depth, written and
  read every frame: 32 bytes a pixel here, which is about 265 MB per frame
  at 4K before any lighting is done. This is why real G-buffers
  are packed so tightly.
- **Transparency.** One surface per texel means glass, smoke and water cannot
  be deferred; they are drawn forward afterwards, as the markers are here.
- **Antialiasing.** Hardware MSAA (chapter 19) resolves colours, but a
  G-buffer holds positions and normals, and averaging those is meaningless.
  Deferred renderers usually antialias the final image instead, with a
  post-process filter.
- **One lighting model.** The lighting pass treats every pixel the same. A
  scene with several kinds of material has to store a material id in the
  G-buffer and branch on it.

In exchange: a light costs only the pixels it reaches, and adding lights does
not make the geometry slower.

## Try it yourself

1. Change `lightCount` to 16, then to 256. *Hint: the shader never needs
   recompiling for a different count, because the loop is a real GPU loop;
   only the C++ constants that size the arrays change.*
2. Remove the light volume test by making the `ifThen` condition always true
   (for example `distance < 1000.0f`). The picture should look the same. How
   does the frame rate compare with many lights? *Hint: make the window large,
   and raise the light count until the difference is obvious.*
3. Make the radius visible: add a small amount of colour, say 0.02 times the
   light's colour, for every pixel inside a light's radius regardless of
   attenuation. You should see a faint disc around each light. *Hint: do it
   inside the `ifThen`.*
4. Drop the position attachment and rebuild the position from depth. Give the
   lit target `sampleableDepth`, read its depth in the lighting pass, and
   invert the view-projection on the CPU. *Hint: the depth plane cannot be
   read in a pass that also has it attached, so the markers need another
   home; and see the DepthSampling example in EACP for the depth read.*
5. Give each cube its own albedo, for instance from its grid position, and
   watch the albedo inset change while the lighting code stays untouched.
   *Hint: this is the separation deferred shading buys you.*

## Further reading

- EACP GPU README, [Several targets at once](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#several-targets-at-once): `beginMultiTargetPass`, `ColorTarget` and `extraColorTargets`, and why a multi-output shader is hand-written today.
- EACP GPU README, [Depth](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#depth): `RenderPassDescriptor::depthTarget`, `DepthAction` and `sampleableDepth`.
- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has): `var`, `loop`, `ifThen` and `select`.
- `Apps/GPU/DepthSampling` in EACP: reading a finished pass's depth buffer in a later pass.
- learnopengl.com, [Deferred Shading](https://learnopengl.com/Advanced-Lighting/Deferred-Shading).
- Ola Olsson and Ulf Assarsson, "Tiled Shading" (2011), and Emil Persson's notes on clustered shading, for the next step after light volumes.

This is the last chapter of the series for now. On learnopengl.com the
Advanced Lighting section goes on to screen-space ambient occlusion (SSAO),
which reads the very G-buffer you built here, and the PBR section replaces
Phong with physically based shading. Both make good next projects with the
tools these chapters have covered.

Previous: [22. HDR and Bloom](../22-HDRBloom/README.md)
