# 12. Lighting Maps

The cube in chapter 09 (Lighting) was made of one stuff: a single orange colour
and one fixed shininess, everywhere on its surface. Real objects are mixtures.
A wooden crate held together with a steel frame has a dull, brown, grainy part
and a grey, polished part, and the two respond to the same lamp in quite
different ways: the steel catches a sharp glint, the wood barely shines at all.
In this chapter you will first give the lighting a proper vocabulary, a
**material** that describes the surface and a **light** that describes the
lamp, each with its own ambient, diffuse and specular properties. Then you will
let those properties vary across the surface by reading them from textures:
a **diffuse map** for the colour and a **specular map** for the shine. Both
textures are generated in code, a crate of wooden planks in a riveted steel
frame, and the light that circles it glints off the metal and not the wood.

## Materials

Chapter 09's fragment shader had the whole surface description baked into it:

<!-- snippet: illustrative -->
```cpp
auto ambient = 0.12f;
auto diffuse = max(dot(n, l), 0.0f);
auto specular = 0.6f * pow(max(dot(v, r), 0.0f), shininess);

auto lit = (ambient + diffuse) * objectColor + specular;
```

One colour, `objectColor`, did all the work, and the strengths `0.12` and
`0.6` were constants. To describe different materials we want each Phong term
to have its own knob. A **material** is that set of knobs:

- **ambient colour**: the colour the surface shows in the faint, everywhere
  light of the ambient term;
- **diffuse colour**: the colour it shows under direct light, what you would
  call "the colour of the object";
- **specular colour**: the colour and strength of its highlight. Plastics and
  paints have a white or grey highlight; metals tint the highlight with their
  own colour, which is why gold's highlight is yellow;
- **shininess**: the exponent from chapter 09, how tight the highlight is.

Each term of Phong is then "how much the light gives" times "how much the
surface gives back":

```
  ambient  = light.ambient  * material.ambient
  diffuse  = light.diffuse  * max(n . l, 0)                  * material.diffuse
  specular = light.specular * pow(max(v . r, 0), shininess)  * material.specular

  colour   = ambient + diffuse + specular
```

The products are per component (red times red, green times green, blue times
blue), so a material that reflects no blue looks black under a pure blue light,
just as you saw with the orange cube.

### The light has properties too

Notice that the light now has three colours as well. A real lamp has only one
colour, but splitting it lets a scene decide how strong each term is: a dim
ambient so that shadowed faces are dark but not black, a strong diffuse, and a
full-strength specular so highlights can reach white. The usual way to set them
is one colour scaled three ways, which is exactly what this chapter does:

```
  light.ambient  = colour * 0.15
  light.diffuse  = colour * 0.8
  light.specular = colour * 1.0
```

With both sides spelled out, a material table lets you reproduce real stuff.
A few classic values (from the OpenGL/VRML material tables, reproduced on
learnopengl.com and elsewhere) look like this:

| material | ambient | diffuse | specular | shininess |
| --- | --- | --- | --- | --- |
| gold | 0.25, 0.20, 0.07 | 0.75, 0.61, 0.23 | 0.63, 0.56, 0.37 | 51 |
| emerald | 0.02, 0.17, 0.02 | 0.08, 0.61, 0.08 | 0.63, 0.73, 0.63 | 77 |
| black rubber | 0.02, 0.02, 0.02 | 0.01, 0.01, 0.01 | 0.40, 0.40, 0.40 | 10 |

Rubber has almost no diffuse colour and a soft, grey highlight; gold has a
coloured, fairly tight one. Trying a few of these is an exercise at the end.

## Grouping uniforms

A material is four values and a light is four more. GLSL would write them as
`struct Material { ... }; uniform Material material;`. EACP's shader EDSL has no
aggregate uniform type, and does not need one: the shader is a C++ struct, so a
group of uniforms is simply a C++ struct of `Uniform` members, and the shader
holds one of each:

```cpp
struct Light
{
    Uniform<Float3> position;
    Uniform<Float3> ambient;
    Uniform<Float3> diffuse;
    Uniform<Float3> specular;
};
```

Inside `define()` you write `light.diffuse` and on the CPU
`shader.light.diffuse = ...`, which reads exactly like the GLSL version. The
only thing to remember is that `EACP_SHADER` lists *uniforms*, not structs, so
each member is listed by its full name: `light.position, light.ambient, ...`.
Behind the scenes every member is still its own value in the uniform block;
the struct is purely for you.

## From colours to maps

A material with three colours still paints the whole object the same. The
crate needs the colour, and above all the shininess, to change from point to
point. We already know how to look up a value that changes across a surface:
a texture, sampled at the fragment's UV, exactly as in chapter 06 (Textures).
So we replace the material's colours with textures:

- The **diffuse map** holds the diffuse colour of every texel. It is what you
  would think of as "the crate texture". The ambient colour of almost every
  real surface is the same as its diffuse colour, only dimmer, so we drop the
  separate ambient colour and use the diffuse map for both; the light's
  ambient strength does the dimming.
- The **specular map** holds the specular strength of every texel. Wood is
  matte, so its texels are nearly black; steel is polished, so its texels are
  bright; the dark gaps between planks are fully black. Here it is grey, but a
  coloured specular map could tint the highlights of a metal.

```
   diffuse map                      specular map
  +--------------------+          +--------------------+
  |o  steel  o  steel o|          |####################|   # bright: shines
  | +----------------+ |          |##+--------------+##|
  | | wood  wood     | |          |##|              |##|   . dark: matte
  |o| wood  wood     |o|          |##|   . . . . .  |##|
  | | wood  wood     | |          |##|              |##|
  | +----------------+ |          |##+--------------+##|
  |o  steel  o  steel o|          |####################|
  +--------------------+          +--------------------+
```

The two maps describe one surface, so they must line up texel for texel. When
artists paint them, they paint the specular map on top of the diffuse map; here
we compute both from the same layout in one loop, which lines them up by
construction.

Shininess stays a single uniform. It could be a map too (some engines keep it
in the specular map's alpha channel), and that is an exercise.

## Making the textures

There are no image files in this series, so the crate is generated, like the
checkerboard in chapter 06, only with more ingredients. The one new tool is
**value noise**: a smooth random-looking function of position. Give every
integer grid point a pseudo-random number in 0..1 from a hash, and for points
in between blend the four surrounding values smoothly:

```cpp
// A pseudo-random number in 0..1 for every integer grid point, the same every
// time it is asked for.
float hash(int x, int y)
{
    auto h = static_cast<std::uint32_t>(x) * 374761393u
             + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h ^ (h >> 16)) / 4294967295.0f;
}

// Value noise: the hashes at the four surrounding grid points, blended
// smoothly. Stretching x and y by different amounts gives streaks, which is
// all wood grain and brushed metal need.
float noise(float x, float y)
{
    auto x0 = static_cast<int>(std::floor(x));
    auto y0 = static_cast<int>(std::floor(y));
    auto fx = x - static_cast<float>(x0);
    auto fy = y - static_cast<float>(y0);
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    auto top = hash(x0, y0) + (hash(x0 + 1, y0) - hash(x0, y0)) * fx;
    auto bottom = hash(x0, y0 + 1) + (hash(x0 + 1, y0 + 1) - hash(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}
```

`fx * fx * (3 - 2 * fx)` is the smoothstep curve, which hides the grid: plain
linear blending would show creases along the grid lines. Sampled with very
different scales in x and y, say `noise(x * 0.04, y * 1.7)`, the values change
slowly along x and quickly along y, which is a field of horizontal streaks:
brushed metal, or the fibres in wood.

`makeCrateMaps` walks every texel of a 256x256 image and decides what it is,
then writes a colour into the diffuse image and a strength into the specular
one. The frame is the band within 26 texels of any edge, and the rivets are
small discs at its corners and midpoints:

```cpp
auto edge =
    std::min(std::min(x, y), std::min(size - 1 - x, size - 1 - y));
auto inFrame = edge < frame;
```

Each region picks its colour and its `shine`:

```cpp
if (rivet)
{
    // Domed: lighter towards the top-left, as if lit from there.
    auto dome = 0.85f - 0.04f * (rivetX + rivetY);
    colour = Vec3 {0.62f, 0.64f, 0.67f} * dome;
    shine = 1.0f;
}
else if (inFrame)
{
    // Brushed steel: streaks running along each side of the frame,
    // and a dark groove where the frame meets the wood.
    auto horizontal = std::min(y, size - 1 - y) < frame;
    auto brushed = horizontal ? noise(fx * 0.04f, fy * 1.7f)
                              : noise(fx * 1.7f, fy * 0.04f);
    auto groove = edge >= frame - 3 ? 0.45f : 1.0f;
    colour =
        Vec3 {0.36f, 0.38f, 0.42f} * (0.8f + 0.3f * brushed) * groove;
    shine = (0.65f + 0.3f * brushed) * groove;
}
else
{
    // Planks running left to right. Each has its own tint, the
    // grain is a ripple across it bent by stretched noise, and a
    // thin dark gap separates neighbours.
    // ... which plank this row is in, and whether it is in the gap ...
    auto tint = 0.82f + 0.3f * hash(plank, 7);
    auto bend = 5.0f * noise(fx * 0.025f, fy * 0.08f + plank * 9.0f);
    auto grain = 0.5f + 0.5f * std::sin(fy * 0.8f + bend * 2.0f);
    auto fibre = noise(fx * 0.3f, fy * 3.0f);

    colour = Vec3 {0.58f, 0.36f, 0.18f} * tint
             * (0.7f + 0.22f * grain + 0.12f * fibre);
    shine = 0.04f + 0.06f * grain;
    ...
}
```

The numbers are taste, not theory; what matters is the *ratio* between the two
`shine` values. Steel lands between about 0.65 and 0.95, wood between 0.04 and
0.1, so the same highlight is roughly ten times brighter on the frame than on
the planks. Notice also that the steel's diffuse colour is fairly dark. Polished
metal reflects most of its light as specular, not diffuse, so it looks dim
until the highlight finds it; making it a light grey would make it look like
painted plastic.

Both images become textures the way chapter 06's did, and are returned
together:

```cpp
auto& device = Device::shared();
return {device.makeTexture(descriptor, diffuse.data()),
        device.makeTexture(descriptor, specular.data())};
```

## Walking through the code

The vertex gains a texture coordinate beside chapter 09's position and normal:

```cpp
struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};
```

`makeCube` is chapter 09's, with one extra statement per corner. `across1` points
right across the face seen from outside and `across2` points up, so a corner at
`(-1, +1)` is the top-left one. UV (0, 0) is the top-left of the texture (chapter
06), so the UV is the corner's across-coordinates remapped from -1..1 to 0..1,
with v flipped:

```cpp
auto uv =
    Vec2 {(corners[c][0] + 1.0f) * 0.5f, (1.0f - corners[c][1]) * 0.5f};
mesh.vertices[f * 4 + c] = {position, face.normal, uv};
```

Every face gets the whole texture, upright as seen from outside.

The material and the light are two small structs of uniforms:

```cpp
// What the surface is made of. In chapter 09 this was one colour and a
// constant; now each property is a uniform, and the two colours are textures,
// so they can change from texel to texel.
struct Material
{
    Uniform<Texture2D> diffuse; // the surface's colour (ambient uses it too)
    Uniform<Texture2D> specular; // how strongly each texel reflects highlights
    Uniform<Float> shininess; // how tight those highlights are
};

// What the light gives off. Separate strengths for each Phong term let a scene
// have a dim ambient glow and a bright highlight from the same lamp.
struct Light
{
    Uniform<Float3> position;
    Uniform<Float3> ambient;
    Uniform<Float3> diffuse;
    Uniform<Float3> specular;
};
```

The shader's constructor sets both textures' sampling before `compile()`,
because in EACP the sampler is part of the compiled shader (chapter 06). The
vertex stage is chapter 09's plus a `varying` for the UV. The fragment stage
reads the material at this fragment, one sample from each map, and then
evaluates the three terms of the formula above:

```cpp
// The material at this fragment: one read from each map.
auto surfaceColor = sample(material.diffuse, uv).xyz();
auto surfaceShine = sample(material.specular, uv).xyz();

auto l = normalize(light.position - fragmentPosition);
auto v = normalize(cameraPosition - fragmentPosition);
auto r = reflect(-l, n);

// Phong, term by term, each the light's strength times the
// material's response.
auto ambient = light.ambient * surfaceColor;
auto diffuse = light.diffuse * max(dot(n, l), 0.0f) * surfaceColor;
auto specular = light.specular
                * pow(max(dot(v, r), 0.0f), material.shininess)
                * surfaceShine;

setFragment(float4(ambient + diffuse + specular, 1.0f));
```

Compare it with chapter 09: the structure is identical, every constant became
a property of the light or the material, and the two colours became texture
reads. `pow` now takes its exponent from a uniform instead of a C++ constant;
the EDSL does not mind which.

The members, with the groups listed member by member:

```cpp
Uniform<Float4x4> model;
Uniform<Float4x4> normalMatrix;
Uniform<Float4x4> viewProjection;
Uniform<Float3> cameraPosition;
Material material;
Light light;

EACP_SHADER(model,
            normalMatrix,
            viewProjection,
            cameraPosition,
            material.diffuse,
            material.specular,
            material.shininess,
            light.position,
            light.ambient,
            light.diffuse,
            light.specular)
```

The lamp shader is unchanged from chapter 09.

The view builds the maps first, as a member declared before the shaders so the
textures outlive the shaders that point at them, and hands them to the material
once, after `prepare`. The material does not change, so this is the only place
it is set:

```cpp
crateShader.material.diffuse = maps.diffuse;
crateShader.material.specular = maps.specular;
crateShader.material.shininess = 32.0f;
```

In `render` the light changes every frame, because it moves. Its three
strengths are one colour, a slightly warm white, scaled three ways:

```cpp
crateShader.light.position = lightPosition;
crateShader.light.ambient = lightColor * 0.15f;
crateShader.light.diffuse = lightColor * 0.8f;
crateShader.light.specular = lightColor * 1.0f;
```

The light circles the crate at a little above its centre, and the crate turns
slowly the other way. The heights matter more than they look: on a flat face, a
point light only produces a highlight where the face mirrors it into the
camera, so the lamp has to pass roughly between the camera and a face. Put it
high above the crate and you will see the faces brighten and darken but never
glint.

## What you should see

A wooden crate with a riveted steel frame, turning slowly, with a small warm
lamp circling it. Faces turned towards the lamp show the wood's colour and
grain; faces turned away fall to the dim ambient. Every few seconds, as the
lamp passes in front of a face, a bright spot of light slides across it. Where
the spot crosses the steel frame or a rivet it flares nearly white; where it
crosses the planks you can barely see it, a faint sheen at most. That
difference, from one draw with one shininess and one light, is entirely the
specular map.

## Try it yourself

1. Replace the specular map read with a constant `1.0f`, then with `0.0f`.
   *Hint: with 1 the whole crate looks like varnished plastic; with 0 the steel
   looks like grey cardboard.*
2. Invert the specular map: `1.0f - sample(material.specular, uv).xyz()`.
   *Hint: shiny wood and dull steel is a surprisingly convincing "wrong".*
3. Put the material table back in: give the shader three `Uniform<Float3>`
   colours instead of the two textures and try gold, emerald and black rubber
   from the table above. *Hint: drop the light's 0.15/0.8/1.0 scaling and use
   `lightColor` for all three, as the table values already include the
   strengths.*
4. Add an **emission map**, a third texture that is added to the result
   unlit: `ambient + diffuse + specular + emission`. Generate it black
   everywhere except, say, a glowing stripe or a stencilled number on the
   planks. *Hint: emission ignores the light entirely, so it still glows on the
   face turned away from the lamp.*
5. Keep shininess in the specular map's alpha channel, for example 0.5 for
   steel and 0.1 for wood, and use `8 + 120 * alpha` as the exponent. *Hint:
   the alpha of a sample is `.w()`.*

## Further reading

- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has): why there is no aggregate type, and why a struct of handles is just a C++ struct.
- EACP GPU README, [Sampling](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#sampling): how a texture's sampler belongs to the shader.
- learnopengl.com, [Materials](https://learnopengl.com/Lighting/Materials) and [Lighting maps](https://learnopengl.com/Lighting/Lighting-maps).
- Devernay's [OpenGL/VRML materials table](http://devernay.free.fr/cours/opengl/materials.html), where the table above comes from.
- Inigo Quilez on [value noise](https://iquilezles.org/articles/morenoise/), and the much richer procedural textures it leads to.

Previous: [11. Camera](../11-Camera/README.md) | Next: [13. Light Casters](../13-LightCasters/README.md)
