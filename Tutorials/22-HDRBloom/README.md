# 22. HDR and Bloom

Every colour we have written so far has been a number between 0 and 1, and
anything above 1 was quietly cut off on its way to the screen. That was fine
while our lights were modest. A real scene is not modest: a bare bulb is
hundreds of times brighter than the wall it lights, and the sun is brighter
still. In this chapter you will light a dark corridor with four coloured lights
ten or fifteen times brighter than "white", keep those values intact by
rendering into a **floating-point** texture, and then bring them back into the
0-to-1 range a display can show. Along the way you will meet **high dynamic
range** (HDR) rendering, **tone mapping** with an **exposure** control,
**gamma correction**, and **bloom**: the soft glow that spills around very
bright things, built from a threshold, a **separable Gaussian blur** that
ping-pongs between two textures, and one addition. The window is split down
the middle: the left half shows the same HDR image simply clamped, the right
half tone mapped and bloomed, so you can see what each step buys.

## Low dynamic range, and what clipping does

The window's colour buffer is `BGRA8Unorm`: eight bits per channel, a
**normalised** integer from 0 to 255 standing for 0.0 to 1.0. A fragment shader
can return 7.5 for red, but the hardware has nowhere to put it, so it stores
1.0. This is **clipping** (or clamping), and once it has happened the
information is gone.

Clipping hurts in two ways. First, every value above 1 becomes the *same*
value. A wall lit at 1.2 and a lamp at 12 come out identically white, so the
lamp no longer looks brighter than the wall next to it. Second, it clips each
channel separately. A red light of `(12, 1.5, 0.8)` is overwhelmingly red, but
clamped it becomes `(1, 1, 0.8)`, a pale yellow-white, and its colour has gone:

```
                   red  green  blue
  the light        12    1.5    0.8     unmistakably red
  clamped           1    1      0.8     pale yellow-white
```

The 0-to-1 range of a display is called **low dynamic range** (LDR). We have
been doing our lighting in LDR without saying so, by keeping every light
colour at or below 1 and choosing materials so the sums rarely went over.

## High dynamic range: keep the numbers, decide later

The fix is to separate two decisions that LDR rendering makes at the same
time: *how bright is this point?* and *what should the display show for it?*
We answer the first with real numbers, as large as the scene needs, and only
at the very end map them into 0 to 1.

That means the lighting needs somewhere to store values above 1, and chapter
16 (Framebuffers) has already given us the place: an offscreen texture. We only
change its **format**. EACP's GPU README lists the texture formats, and
recommends one for exactly this:

| Format | What it holds |
| --- | --- |
| `RGBA8Unorm` | 8 bits per channel, 0 to 1. Chapter 16's target. |
| `RGBA16Float` | a 16-bit **half float** per channel: about 3 significant digits, up to 65504 |
| `RGBA32Float` | a full 32-bit float per channel |

`RGBA16Float` is the one to reach for. Half floats have plenty of range for
light (the brightest value it can hold is 65504), they keep relative precision
everywhere, so dark values are not crushed into a few steps as they are in 8
bits, and every GPU that EACP runs on can filter them, which the bloom passes
below rely on. Full floats cost twice the memory for precision no colour needs.

Nothing else about the scene pass changes. The lighting shader is chapter 13's
point lights, now with intensities like 12 and 15, and nothing clamps the
result; the texture stores what it gets.

## Tone mapping

At the end we have an image whose values run from 0 to 15 or more, and a
display that takes 0 to 1. **Tone mapping** is the function that squeezes one
into the other. A good one keeps the dark and middle tones roughly as they
were, and compresses the bright end smoothly instead of cutting it off.

The simplest is **Reinhard** tone mapping:

```
  mapped = hdr / (1 + hdr)
```

At small values `hdr / (1 + hdr)` is about `hdr`, so dark areas are barely
touched. As `hdr` grows it approaches 1 but never reaches it: 1 maps to 0.5, 3
to 0.75, 12 to 0.92. Every brightness keeps its own output value, so a lamp
stays brighter than the wall it lights, and since the three channels are
compressed rather than clipped, a light of `(12, 1.5, 0.8)` maps to
`(0.92, 0.6, 0.44)`, which is still clearly red-orange.

Reinhard has no control: the curve is fixed. A camera has one, the
**exposure**, how long the shutter stays open. The exposure tone mapper builds
that in:

```
  mapped = 1 - exp(-hdr * exposure)
```

It has the same shape: linear near 0, approaching 1 without ever reaching it.
The exposure stretches the curve horizontally. A high exposure brightens the
dark parts and saturates the bright ones sooner, the way a long shot of a
night street shows the buildings but turns the street lamps into blobs; a low
exposure keeps detail in the bright parts and lets the shadows go black.

```
  hdr value                 0.1    0.5    1      3      12
  clamped                   0.1    0.5    1      1      1
  exposure 1, 1 - e^-x      0.10   0.39   0.63   0.95   1.00
  exposure 0.25             0.02   0.12   0.22   0.53   0.95
  Reinhard, x / (1 + x)     0.09   0.33   0.50   0.75   0.92
```

This chapter uses the exposure curve and lets you change the exposure with the
scroll wheel. Real renderers usually go further and measure the image's
average brightness each frame, adjusting the exposure automatically the way
your eyes adapt when you walk out of a dark room; one of the exercises starts
in that direction.

## Gamma correction

There is one more step between "a value in 0 to 1" and "what the display
shows", and it is one every earlier chapter has skipped.

Our lighting maths works in **linear** light: double the number, double the
light. Displays do not show their input linearly. For historical reasons (CRT
electronics, and the happy accident that it matches how the eye perceives
brightness) a display shows a pixel value `v` as roughly `v^2.2` of its full
brightness. A value of 0.5 comes out at about 0.22 of full brightness, not
half. Images are stored to match: an 8-bit colour value is **gamma encoded**,
`light^(1/2.2)`, which also happens to spend more of the 256 steps on dark
tones, where the eye is most sensitive.

So a renderer that writes linear light straight to the screen shows every
mid-tone too dark, and too contrasty: a surface lit at 0.5 is displayed at
0.22. The correction is to encode before the value leaves the shader:

```
  encoded = linear^(1 / 2.2)
```

The precise standard curve is **sRGB**, a short linear segment near black and
a 2.4 power above it, which `^(1/2.2)` approximates closely. EACP has
`RGBA8UnormSrgb` and `BGRA8UnormSrgb` formats that do this encoding in
hardware when you render into them (and the decoding when you sample a texture
stored in them). The window's drawable here is plain `BGRA8Unorm`, so we
encode ourselves, with a `pow` in the last shader.

Why have earlier chapters looked fine? Because their colours were chosen by
eye on a display, which means they were effectively chosen in gamma space, and
the light was never very physical anyway. Once lights get bright and fall off
with distance the way real light does, the difference shows: without
correction, the inverse-square fall-off from a lamp looks like a tiny bright
spot dropping off a cliff into black.

Gamma correction must be the **last** step. Tone mapping and blending are
linear operations and expect linear values; encoding first and then adding
bloom would add gamma-encoded numbers, which is not adding light.

## Bloom

Look at a bright light at night and it seems surrounded by a soft glow. That
glow happens in your eye and in camera lenses: a little of the light is
scattered sideways on its way to the sensor. On a display, which cannot be
bright enough to dazzle anyone, imitating that glow is the most convincing way
to say "this is much brighter than white".

Bloom is built from three steps on the HDR image:

```
  scene (HDR) --threshold--> bright parts --blur, blur, blur--> glow
       |                       (half size)                        |
       +-----------------------------  +  <-----------------------+
                                       |
                              tone map, gamma  -->  window
```

1. **Extract the bright parts.** Keep only pixels brighter than a threshold
   (1.0 here), black elsewhere. In an HDR image that is a meaningful test:
   only the lamps and the walls right next to them pass.
2. **Blur them**, widely.
3. **Add the blur back** onto the HDR scene, before tone mapping, so the glow
   is light like any other and the tone mapper treats it the same way.

### Extracting, softly

A hard threshold, `brightness > 1 ? color : 0`, works, but the glow then pops
on and off as a surface crosses 1, and its edge flickers as the camera moves.
We scale the colour instead by how far above the threshold it is:

```
  keep = max(brightness - threshold, 0) / brightness
```

0 at or below the threshold, 0.5 at twice the threshold, close to 1 for a lamp
at 12. "Brightness" is the colour's **luminance**, the Rec. 709 weighted sum
from chapter 16's grayscale effect, so a saturated blue light is judged as
bright as it looks rather than by its largest channel.

### Separable Gaussian blur

The blur that looks most like scattered light is a **Gaussian**: each output
pixel is a weighted average of its neighbourhood, with weights following a bell
curve, `exp(-d^2 / (2 sigma^2))`, where `d` is the distance in texels and
`sigma` sets the width. Chapter 16's 3x3 blur was a tiny Gaussian. Bloom wants
a much wider one: a glow tens of pixels across.

A 2D kernel 21 texels wide is 441 samples per pixel. The trick is that the 2D
Gaussian is **separable**: it is a 1D Gaussian across, followed by a 1D
Gaussian down. Blurring every row with 21 samples, then every column of the
result with 21 more, gives exactly the same image for 42 samples instead of
441.

```
   2D kernel                 =    horizontal pass    then    vertical pass

   . : o O o : .                  . : o O o : .                  .
   : o O @ O o :                                                 :
   o O @ # @ O o                                                 o
   O @ # # # @ O                                                 O
   o O @ # @ O o                                                 o
   : o O @ O o :                                                 :
   . : o O o : .                                                 .
```

Each pass needs to read the previous pass's whole image, and a pass cannot read
the texture it is writing (chapter 16). So the blur uses **two** textures and
draws back and forth between them, which is called **ping-pong**:

```
  bright pass ------> [bloom 0]
  horizontal blur:    [bloom 0] --read--> write --> [bloom 1]
  vertical blur:      [bloom 1] --read--> write --> [bloom 0]
  ... repeated; the glow ends up in bloom 0 each time
```

Running the horizontal-vertical pair several times widens the blur further:
blurring a Gaussian with a Gaussian gives a Gaussian whose sigma grows with the
square root of the number of rounds. Six rounds of sigma 5 behave like one
blur of sigma about 12.

### At half size

The glow is blurry by definition, so there is no point computing it at full
resolution. The two bloom textures are **half the size** of the scene in each
direction: a quarter of the pixels, and every blur sample covers twice the
distance on screen, so the glow is twice as wide for the same work.

Shrinking has a free bonus. A pixel of the half-size target sits exactly at
the corner where four scene texels meet, so sampling the scene there with
**linear** filtering returns the average of those four. The bright pass
downsamples properly without a single extra line. Going the other way, the
final pass samples the small bloom texture with linear filtering too, which
smoothly enlarges it back to full size.

## Walking through the code

The scene is chapter 13's: a cube mesh, a `LitShader` that reads point lights
from a storage buffer, and an unlit `LampShader` for the light markers. A light
record is now only a position and a colour, and the colour carries the
intensity:

```cpp
const std::array<PointLight, lightCount> lights = {{
    {{1.2f, 0.6f, -2.5f}, {12.0f, 1.5f, 0.8f}}, // red
    {{-1.3f, 2.2f, -6.5f}, {1.0f, 2.5f, 14.0f}}, // blue
    {{1.0f, 1.9f, -11.0f}, {1.5f, 10.0f, 2.0f}}, // green
    {{0.0f, 1.3f, -16.0f}, {15.0f, 11.0f, 6.0f}}, // warm white
}};
```

The lighting loop is chapter 13's `pointLight` written inline, with a simpler
fall-off: one over the squared distance, plus one so it stays finite right at
the light. The lamp cubes are drawn in the light's colour as it is, 12 or 15,
so they are by far the brightest things in the image.

```cpp
        for (auto i = 0u; i < lightCount; ++i)
        {
            auto toLight = lights.read3(2 * i) - p;
            auto distance = length(toLight);
            auto l = toLight / distance;
            auto arriving =
                lights.read3(2 * i + 1) / (1.0f + 2.0f * distance * distance);

            auto diffuse = max(dot(n, l), 0.0f);
            auto r = reflect(-l, n);
            auto specular = 0.3f * pow(max(dot(v, r), 0.0f), 32.0f);

            color = color + (diffuse * albedo + specular) * arriving;
        }
```

### The targets

All three textures are `RGBA16Float` render targets; only the scene has a
depth buffer, because only the scene draws 3D geometry:

```cpp
    static Texture makeTarget(int width, int height, bool depth)
    {
        auto descriptor = TextureDescriptor {};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.format = hdrFormat;
        descriptor.renderTarget = true;
        descriptor.depth = depth;
        return Device::shared().makeTexture(descriptor);
    }
```

They are rebuilt whenever the view's pixel size changes, as in chapter 16, and
that is also where every program is told which texture it reads. Because each
blur program always reads the same texture (the horizontal one bloom 0, the
vertical one bloom 1), this wiring is done once per resize, not every frame.
The blur step is one texel of the *bloom* texture, along one axis:

```cpp
        sceneTarget.emplace(makeTarget(width, height, true));
        bloomTargets[0].emplace(makeTarget(bloomWidth, bloomHeight, false));
        bloomTargets[1].emplace(makeTarget(bloomWidth, bloomHeight, false));

        brightShader.scene = *sceneTarget;
        blurHorizontal.source = *bloomTargets[0];
        blurVertical.source = *bloomTargets[1];
        compositeShader.scene = *sceneTarget;
        compositeShader.bloom = *bloomTargets[0];

        blurHorizontal.texelStep = Vec2 {1.0f / (float) bloomWidth, 0.0f};
        blurVertical.texelStep = Vec2 {0.0f, 1.0f / (float) bloomHeight};
```

The pipelines follow chapter 16's rule that a pipeline must match what it
draws into. The scene, bright and blur programs all draw into `RGBA16Float`
textures, so they are prepared with `pixelFormatFor(hdrFormat)` and one
sample; only the composite program draws into the window and is prepared for
it with `prepare(sampleCount())`.

### The bright pass

`luminance` is a plain C++ function on handles, as in chapter 13. It spells
out the weighted sum instead of using `dot` with a `float3` of weights, because
building a vector from literals needs a `constant()` handle, and `constant()`
belongs to a `ShaderProgram`, which a free function does not have.

```cpp
Float luminance(const Float3& color)
{
    return 0.2126f * color.x() + 0.7152f * color.y() + 0.0722f * color.z();
}
```

The shader samples the scene with linear filtering (the free 2x2 average) and
applies the soft threshold:

```cpp
        auto color = sample(scene, uv).xyz();
        auto brightness = luminance(color);

        auto keep = max(brightness - threshold, 0.0f) / max(brightness, 0.0001f);
        setFragment(float4(color * keep, 1.0f));
```

The `max(brightness, 0.0001f)` keeps a black pixel from dividing zero by zero.

### The blur

The Gaussian weights are computed on the CPU, in ordinary C++, and normalised
so the whole kernel (the centre once, each other weight twice, once per side)
sums to 1. A blur whose weights summed to more than 1 would brighten the glow
every round.

```cpp
std::array<float, blurRadius + 1> gaussianWeights(float sigma)
{
    auto weights = std::array<float, blurRadius + 1> {};
    auto total = 0.0f;

    for (auto i = 0; i <= blurRadius; ++i)
    {
        weights[i] = std::exp(-(float) (i * i) / (2.0f * sigma * sigma));
        total += i == 0 ? weights[i] : 2.0f * weights[i];
    }

    for (auto& weight: weights)
        weight /= total;

    return weights;
}
```

`BlurShader::define()` calls it while the graph is being recorded and unrolls
the loop, exactly like chapter 16's `convolve`: the compiled shader holds 21
`sample` calls with the weights baked in as literals. One program serves both
directions, because the direction is entirely in the `texelStep` uniform:
`(1/width, 0)` walks along a row, `(0, 1/height)` down a column. We still make
two instances, `blurHorizontal` and `blurVertical`, because each reads a
different texture.

```cpp
        auto weights = gaussianWeights(5.0f);
        auto sum = sample(source, uv).xyz() * weights[0];

        for (auto i = 1; i <= blurRadius; ++i)
        {
            auto offset = texelStep * (float) i;
            sum = sum + sample(source, uv + offset).xyz() * weights[i];
            sum = sum + sample(source, uv - offset).xyz() * weights[i];
        }

        setFragment(float4(sum, 1.0f));
```

### The composite

The last pass reads the scene and the glow, and produces both halves of the
window. The left half clamps, as the window would have done on its own. The
right half adds the glow and tone maps. `select` chooses per pixel by which
side of the divider the pixel is on, and then both halves are gamma encoded,
so the only difference between them is tone mapping and bloom:

```cpp
        auto hdr = sample(scene, uv).xyz();

        auto clipped = clamp(hdr, 0.0f, 1.0f);

        auto withBloom = hdr + sample(bloom, uv).xyz() * bloomStrength;
        auto mapped = 1.0f - exp(-withBloom * exposure);

        auto linear = select(uv.x() < split, clipped, mapped);

        auto encoded = pow(linear, 1.0f / 2.2f);
```

A second `select` paints a thin divider line where `uv.x()` is within half a
point of `split`.

### The frame

`render` records the passes in order. Each pass is its own block, so it ends
before the next one samples what it wrote, and the blur rounds alternate
targets:

```cpp
        // Pass 1: the scene, in HDR.
        {
            auto pass =
                frame.beginPass(*sceneTarget, {Graphics::Color {0.0f, 0.0f, 0.0f}});
            drawScene(pass, (float) width / (float) height);
        }

        // Pass 2: the bright parts, at half size.
        {
            auto pass = frame.beginPass(*bloomTargets[0]);
            brightShader.threshold = 1.0f;
            pass.draw(brightShader);
        }

        // Pass 3: blur, horizontally then vertically, several times over. Each
        // round leaves its result back in bloom 0.
        for (auto i = 0; i < blurRounds; ++i)
        {
            {
                auto pass = frame.beginPass(*bloomTargets[1]);
                pass.draw(blurHorizontal);
            }

            auto pass = frame.beginPass(*bloomTargets[0]);
            pass.draw(blurVertical);
        }

        // Pass 4: combine, tone map and gamma-correct, into the window.
        compositeShader.exposure = exposure;
        compositeShader.bloomStrength = 1.0f;
        compositeShader.split = split;

        auto pass = frame.beginPass();
        pass.draw(compositeShader);
```

That is fifteen passes a frame, all on one command buffer with no waiting
between them. The twelve blur passes run at a quarter of the window's pixel
count, so together they cost about as much as three full-screen passes.

Input is two small handlers. A click or drag moves the divider to the mouse's
x position (in points, so it is divided by the view's width in points). The
scroll wheel multiplies the exposure by `exp(step)`, so every notch changes it
by the same ratio, which is how exposure feels natural to adjust (a camera's
f-stops work the same way).

## What you should see

A long, dark stone corridor with crates along its sides, lit by four small
glowing cubes: red near the camera on the right, blue further back on the left,
green beyond it, and a warm white one at the far end. The camera sways gently
from side to side. A thin vertical line splits the window.

On the **left** (clamped), every lamp is a flat white square with a hard edge,
whatever its colour: the blue lamp looks exactly like the white one. The walls
right beside the lamps flatten into patches of a single saturated colour,
with the checkerboard washed out of them.

On the **right** (tone mapped and bloomed), the red lamp has a pale core
fading into a red-orange halo that spills over the crate in front of it and
onto the wall; the green and warm white lamps at the back glow too, and the
lit walls keep their colour and checkerboard. Drag the divider across a lamp
to compare the two directly. Scroll down to lower the exposure: the corridor
darkens and the lamps' cores regain their colour. Scroll up and it brightens
until even the right half starts to look washed out.

## Try it yourself

1. Switch the right half to Reinhard: `withBloom / (1.0f + withBloom)`. Then
   try `withBloom * exposure / (1.0f + withBloom * exposure)`. *Hint: Reinhard
   is gentler on the highlights; compare the lamps' cores.*
2. Remove the `pow(..., 1.0f / 2.2f)` and look at the right half. *Hint: the
   pools of light shrink to small, harsh spots, and the far end of the corridor
   goes black: that is what every chapter before this one has looked like with
   physical fall-off.*
3. Turn the bloom up to 3 and down to 0.3, and change `blurRounds` to 1 and
   to 12. *Hint: rounds widen the glow, strength brightens it; real lenses give
   a wide, faint glow, so a low strength and many rounds usually looks best.*
4. Make the bloom targets a quarter of the scene size instead of half. *Hint:
   the glow doubles in width for a quarter of the cost; but sampling the scene
   at quarter size with one linear tap now averages only 4 of every 16 texels,
   so small lamps flicker as they move. Taking four linear samples around the
   centre fixes it.*
5. Halve the blur's samples with the **linear sampling trick**: one linear
   sample placed between texels `i` and `i + 1`, at the right fraction, returns
   their weighted sum. *Hint: for weights `a` and `b`, sample at offset
   `(i * a + (i + 1) * b) / (a + b)` and weight the result by `a + b`.*
6. Animate the exposure towards a target derived from the scene's brightness:
   compute the average luminance of the half-size bright pass on the CPU with
   `Texture::read`, or of a further-shrunk copy, and ease `exposure` towards
   `0.5 / average` each frame. *Hint: this is automatic exposure, the eye
   adapting; keep the easing slow, about a second.*

## Further reading

- EACP GPU README, [Texture formats](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#texture-formats): why `RGBA16Float` is the float format to reach for, and what the sRGB formats do when sampled and rendered into.
- EACP GPU README, [Rendering into a texture](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#rendering-into-a-texture): passes on one frame, and the two-textures-and-a-swap answer to reading what you write.
- learnopengl.com, [HDR](https://learnopengl.com/Advanced-Lighting/HDR), [Bloom](https://learnopengl.com/Advanced-Lighting/Bloom) and [Gamma Correction](https://learnopengl.com/Advanced-Lighting/Gamma-Correction).
- Daniel Rákos, [Efficient Gaussian blur with linear sampling](https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/), for exercise 5.
- John Hable, [Filmic Tonemapping Operators](http://filmicworlds.com/blog/filmic-tonemapping-operators/), for the curves games use beyond Reinhard.

Previous: [21. Normal Mapping](../21-NormalMapping/README.md) | Next: [23. Deferred Shading](../23-DeferredShading/README.md)
