# 06. Textures

So far every colour on screen was either stored per vertex or computed by a
formula. Real scenes are full of detail that is neither: wood grain, brick,
text, photographs. That detail comes from **textures**, images that live in
GPU memory and that a shader can look up at any position. In this chapter you
generate a small image on the CPU, upload it, and draw it on two quads
side by side. The two quads use the same texture but different **samplers**,
so you can see with your own eyes what *nearest* and *linear* filtering do,
and what *repeat* and *clamp* do when you read past the edge of the image.
Finally you multiply each sample by a per-vertex colour, the simplest example
of combining a texture with other data.

## Texels, pixels and UVs

A texture is a grid of colour values. To keep them apart from the pixels on
your screen, the elements of a texture are called **texels** (texture
elements). The two are rarely one-to-one: a 16×16 texture stretched across a
300×300 pixel quad means each texel covers roughly 19×19 pixels, and a 1024×1024
texture on a distant object might squeeze many texels into a single pixel.

Because a texture can be any size, shaders do not address it by texel index.
They use **texture coordinates**, usually called **UV**, which run from 0 to 1
across the whole image whatever its resolution. In EACP, as in Metal and
Direct3D, the first row of pixel data you upload is the top of the image, and
UV (0, 0) is that row's first texel, the **top-left** corner:

```
  u ->
v (0,0) +-------+-------+ (1,0)        row 0 of your pixel data
|       |       |       |
v       |    texture    |
        |       |       |
  (0,1) +-------+-------+ (1,1)        last row of your pixel data
```

Note that this is the opposite of the UVs you invented in the previous
chapter, which grew upwards. That was a free choice; this is a convention of
the API, and you can see it in this chapter's image: the gradient's blue
corner (texel (0, 0)) appears at the top-left of each tile.

You give each vertex a UV, the rasterizer interpolates it across the triangle
like any other varying, and every fragment asks the texture "what colour is at
this UV?".

## Samplers: what happens between and beyond texels

A UV almost never lands exactly on a texel's centre, and it may not even be
inside 0..1. Deciding what colour to return is the job of the **sampler**. It
answers two questions.

**Filtering**: what to do when the UV falls between texel centres.

- **Nearest** returns the single closest texel. Magnified, the texture becomes
  crisp blocks; this is what pixel art wants.
- **Linear** blends the four nearest texels, weighted by how close the UV is
  to each. Magnified, the texture becomes smooth (and blurry); this is what
  photographs and most 3D surfaces want.

```
 texels:   [A][B]         nearest:  AAAAAABBBBBB
                          linear:   AAAAaabbBBBB   (a smooth ramp from A to B)
```

Filtering matters in both directions. **Magnification** is when one texel
covers many pixels (a small texture drawn large, as in this chapter).
**Minification** is the opposite: many texels fall inside one pixel, and
nearest filtering then picks one of them almost at random, which shimmers as
things move. The full answer to minification is *mipmapping*, a chain of
pre-shrunk copies of the texture, which EACP supports but which is beyond this
chapter.

**Wrapping** (the *address mode*): what to do when the UV is outside 0..1.

- **Repeat** uses only the fractional part, so the texture tiles: UV 1.25 reads
  the same as 0.25. This is how a small brick texture covers a large wall.
- **Clamp** clamps the UV into 0..1, so everything outside reads the edge
  texels, which are smeared out to infinity.

```
 u:          0      1      2              0      1      2
 repeat:     |ABCDEF|ABCDEF|    clamp:    |ABCDEF|FFFFFF|
```

### In EACP, the sampler belongs to the shader

In many APIs you attach sampler state to the texture or bind it beside the
texture for each draw. EACP instead makes it part of the *shader's
declaration*: each `Uniform<Texture2D>` member has a `sampling` field (a
`TextureSampling` holding a `TextureFilter` and a `TextureAddressMode`), and
it is baked in when `compile()` runs. The reason is a real driver bug on some
Windows machines that made per-texture samplers unreliable, and it is told in
full in [`SAMPLERS.md`](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/SAMPLERS.md).
The practical consequence is that one compiled shader samples one way, so to
show two samplers at once this chapter builds two shader objects.

## Walking through the code

### The vertices

```cpp
struct Vertex
{
    Vec2 position;
    Vec2 uv; // where in the texture this corner reads from
    Vec3 tint; // multiplied into the sample
};

constexpr auto white = Vec3 {1.0f, 1.0f, 1.0f};
constexpr auto orange = Vec3 {1.0f, 0.55f, 0.25f};
```

Each vertex has a position, a UV and a tint colour. The two quads differ only
in where they are placed:

```cpp
// Two quads side by side. Their UVs run 0..2, past the texture's 0..1 edge, so
// what the sampler does outside the texture is on show. UV (0, 0) is the
// texture's first texel, row 0, which is the top of the image, so it goes on
// the top-left corner.
const Vertex leftQuad[] = {
    {{-0.9f, -0.68f}, {0.0f, 2.0f}, orange}, // bottom left
    {{-0.05f, -0.68f}, {2.0f, 2.0f}, orange}, // bottom right
    {{-0.05f, 0.68f}, {2.0f, 0.0f}, white}, // top right
    {{-0.9f, 0.68f}, {0.0f, 0.0f}, white}, // top left
};

const Vertex rightQuad[] = {
    {{0.05f, -0.68f}, {0.0f, 2.0f}, orange},
    {{0.9f, -0.68f}, {2.0f, 2.0f}, orange},
    {{0.9f, 0.68f}, {2.0f, 0.0f}, white},
    {{0.05f, 0.68f}, {0.0f, 0.0f}, white},
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};
```

The UVs go from 0 to **2**. Only the top-left quarter of each quad lies
inside the texture; the other three quarters are where the address mode
decides what you see.

### Making the texture

```cpp
// A 16x16 RGBA8 image: four bytes per texel (red, green, blue, alpha, each
// 0..255), rows stored top to bottom. A 4x4 checkerboard whose light squares
// carry a gradient, red growing to the right and green growing downwards, so
// the top-left texel is blue and the bottom-right one is near white.
Texture makeCheckerTexture()
{
    constexpr auto size = 16;
    constexpr auto cell = 4;

    auto pixels = std::vector<std::uint8_t>(size * size * 4);

    for (auto y = 0; y < size; ++y)
    {
        for (auto x = 0; x < size; ++x)
        {
            auto light = (x / cell + y / cell) % 2 == 0;
            auto shade = light ? 1.0f : 0.3f;

            auto* texel = &pixels[(y * size + x) * 4];
            texel[0] = static_cast<std::uint8_t>(255.0f * shade * x / (size - 1));
            texel[1] = static_cast<std::uint8_t>(255.0f * shade * y / (size - 1));
            texel[2] = static_cast<std::uint8_t>(255.0f * shade * 0.8f);
            texel[3] = 255;
        }
    }
```

An image in memory is just bytes. `RGBA8` means four channels (red, green,
blue, alpha) of eight bits each, so one texel is four bytes and the whole
image is `16 × 16 × 4` bytes. Rows are stored one after another, so texel
`(x, y)` starts at byte `(y * width + x) * 4`. Alpha is opacity; 255 is fully
opaque.

The pattern is chosen to make orientation obvious: a checkerboard of 4×4-texel
squares, with red increasing to the right and green increasing downwards.

```cpp
    auto descriptor = TextureDescriptor {};
    descriptor.width = size;
    descriptor.height = size;
    descriptor.format = TextureFormat::RGBA8Unorm;

    // Copies the pixels into GPU memory; the vector can go away afterwards.
    return Device::shared().makeTexture(descriptor, pixels.data());
}
```

A `TextureDescriptor` describes the texture: its size and its **format**.
`RGBA8Unorm` is four 8-bit channels, *unsigned normalised*, which means the
shader reads the byte 0 as 0.0 and 255 as 1.0. (It is the default, but naming
it says what the bytes mean.) `Device::shared()` is the GPU your views draw
with, and `makeTexture` creates the texture and copies the pixels in, expecting
tightly packed four-byte texels with row 0 at the top.

### The shader

```cpp
    // How the texture is filtered and wrapped is baked into the shader, so it
    // has to be set before compile() records it.
    explicit TexturedShader(TextureSampling sampling)
    {
        image.sampling = sampling;
        compile();
    }
```

Since the sampler is part of the shader, it is a constructor argument, set on
the texture member before `compile()`.

```cpp
    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto uv = varying(vertexInput(&Vertex::uv));
        auto tint = varying(vertexInput(&Vertex::tint));

        setPosition(float4(position, 0.0f, 1.0f));

        // sample() asks the sampler for the colour at this UV. It returns a
        // Float4 (r, g, b, a) in 0..1, whatever the bytes were stored as.
        auto texel = sample(image, uv);

        // Texture times colour: white leaves the texture as it is, orange
        // keeps its red and removes some of its green and blue.
        setFragment(float4(texel.xyz() * tint, texel.w()));
    }

    // A texture is a uniform too: one image bound for the whole draw.
    Uniform<Texture2D> image;

    EACP_SHADER(image)
```

`Uniform<Texture2D>` is the same idea as the uniforms in chapter 04 (Uniforms): one value
for the whole draw, listed in `EACP_SHADER`, and assigned from C++. In
`define()` it is a handle you pass to `sample(image, uv)`, which records a
texture lookup in the fragment stage.

`texel.xyz()` is a swizzle taking red, green and blue; `texel.w()` is alpha.
Multiplying two colours componentwise is called **modulation**, and it is how
a texture is combined with a colour all over graphics: vertex colours, a
material's tint, the amount of light falling on a surface. White (1, 1, 1)
changes nothing; orange keeps all of the red and some of the green and blue.
The tint is a varying, so it fades from white at the top of each quad to
orange at the bottom.

### The view

```cpp
    TexturesView()
        : checker(makeCheckerTexture())
    {
        nearest.setVertices(leftQuad);
        nearest.setIndices(quadIndices);
        nearest.prepare(sampleCount());
        nearest.image = checker;

        linear.setVertices(rightQuad);
        linear.setIndices(quadIndices);
        linear.prepare(sampleCount());
        linear.image = checker;
    }

    void render(Frame& frame) override
    {
        auto pass = frame.beginPass({Graphics::Color {0.10f, 0.10f, 0.12f}});
        pass.draw(nearest);
        pass.draw(linear);
    }
```

Both shaders point at the same texture. Assigning a texture to the uniform
stores a pointer, not a copy, and `draw()` binds it. The texture never
changes, so it is assigned once in the constructor rather than every frame.
(EACP refuses `shader.image = makeCheckerTexture();` at compile time, since
that would point at a temporary destroyed at the end of the line.)

```cpp
    // The shaders only point at the texture, so it is declared first and
    // therefore outlives them.
    Texture checker;

    TexturedShader nearest {{TextureFilter::Nearest, TextureAddressMode::Repeat}};
    TexturedShader linear {{TextureFilter::Linear, TextureAddressMode::Clamp}};
```

C++ destroys members in reverse order of declaration, so declaring `checker`
first guarantees it outlives both shaders.

### What you see

On the **left** (nearest, repeat) the texture appears four times in a 2×2
grid. UV 1..2 wraps back to 0..1. Every texel is a sharp-edged block,
many screen pixels on a side, and within each light square you can see the
gradient advancing in visible steps, one per texel.

On the **right** (linear, clamp) only the top-left quarter shows the
texture, and it looks soft: the checker squares have blurred edges where
neighbouring texels are blended. The rest of the quad is the last column and
last row of texels stretched into long streaks, and the bottom-right quarter
is a single flat colour, the bottom-right texel.

Both quads fade from untinted at the top to orange at the bottom.

## Try it yourself

1. Swap the two samplers and predict what you will see before you run it.
   *Hint: only the two member initialisers change.*
2. Make the left quad tile the texture eight times instead of twice.
   *Hint: change the UVs, not the texture.*
3. Flip the image upside down without touching the pixel loop. *Hint: swap
   the v values of the top and bottom vertices.*
4. Give the texture a transparent hole and see it through to the clear
   colour. *Hint: alpha alone is not enough; the pipeline also needs blending.
   Look up `BlendMode::AlphaBlend` in the "Naming the pipeline's state"
   section of the GPU README.*
5. Animate the texture scrolling across the quad. *Hint: add a
   `Uniform<Float> time` as in chapter 04 (Uniforms) and sample at
   `uv + float2(time * 0.2f, 0.0f)`, with `Repeat`.*

## Further reading

- EACP [`SAMPLERS.md`](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/SAMPLERS.md):
  why the shader owns its sampling, and how per-draw samplers work for
  hand-written shaders.
- EACP GPU README, [Sampling](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#sampling)
  and [Texture formats](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#texture-formats):
  the full list of formats, including the sRGB ones you will want for colour
  images.
- EACP GPU README, [Mipmaps](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#mipmaps):
  the answer to minification.
- [`Apps/GPU/Texture`](https://github.com/eyalamirmusic/eacp/blob/main/Apps/GPU/Texture/Main.cpp):
  EACP's minimal textured-quad example.
- learnopengl.com, [Textures](https://learnopengl.com/Getting-started/Textures):
  the same ideas in OpenGL, with good pictures of filtering and wrapping
  (note that OpenGL puts UV (0, 0) at the bottom left).

Previous: [05. Fragment Playground](../05-FragmentPlayground/README.md) | Next: [07. Transformations](../07-Transformations/README.md)
