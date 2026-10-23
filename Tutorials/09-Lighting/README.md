# 09. Lighting

The cube from the last chapter had a flat colour painted on each face, so it
only looked solid because the six colours were different. Real objects look
solid because of *light*: a face turned towards a lamp is bright, a face turned
away is dark, and a shiny surface shows a bright spot where the lamp reflects
towards your eye. In this chapter you will light a single-coloured cube with a
point light that circles it, using the classic **Phong lighting model**
computed for every pixel. Along the way you will learn what a **normal** is,
why lighting is done in **world space**, how normals have to be transformed
differently from positions, and what each of Phong's three terms (ambient,
diffuse and specular) is approximating.

## Normals

To know how much light a surface catches, we have to know which way it faces.
That direction is the surface **normal**: a unit vector perpendicular to the
surface, pointing out of it.

```
            n (normal)
            ^
            |
            |
   =========+=========   surface
```

A triangle mesh stores normals per vertex, as one more vertex attribute beside
the position. For a flat-faced cube every point on a face has the same normal,
the face's outward axis, so each face gets four vertices of its own carrying
that normal. This is the same reason the last chapter needed 24 vertices for
per-face colours: a cube corner belongs to three faces, and so has three
different normals. (A smooth object like a sphere would share vertices and let
the rasterizer blend the normals between them.)

## Where the lighting happens: world space

Lighting compares directions: from the surface to the light, from the surface
to the eye, and the surface's normal. Those comparisons only make sense if all
the vectors are in the **same coordinate space**. The light and the camera are
naturally described in world space ("the lamp is at (1.6, 0.7, 0)"), so we
take the surface into world space too:

- the position with the **model** matrix, as a point (w = 1);
- the normal with (a version of) the model matrix, as a direction (w = 0, so
  the translation does not move it).

The clip-space position for `setPosition` is computed separately with the view
and projection matrices. This is why this chapter sends `model` and
`viewProjection` as separate uniforms instead of last chapter's single combined
matrix: the shader now needs the halfway point, world space, for itself.

## Transforming normals: the normal matrix

Rotating a surface rotates its normal the same way, so for rotations (and
uniform scales, followed by a renormalise) the model matrix works fine on
normals. A **non-uniform scale** breaks it:

```
   a slope and its normal          squash y by half with the model matrix
                                   (the normal squashed the same way)
        n                                 n
         \   /                            \
          \ /  surface                     `-._ surface    <- n is no longer
          /                                   _/              perpendicular
         /                               __.-'
```

Squashing a surface makes it flatter, so its normal should become *more*
vertical; squashing the normal the same way makes it less vertical. The
matrix that transforms normals correctly is the **inverse transpose** of the
model matrix, usually called the **normal matrix**. (For a pure rotation the
inverse is the transpose, so the inverse transpose is the rotation itself,
which is why the shortcut works there.)

Shading languages give you no `inverse()`: GLSL has one, but Metal and HLSL do
not, and so neither does EACP's shader EDSL. There are two good ways out:

1. Keep every model matrix to rotations, translations and **uniform** scales,
   and use the model matrix on normals directly.
2. Compute the normal matrix on the **CPU**, where `Maths::Mat4` has
   `inverted()` and `transposed()`, and send it as a uniform.

We do the second. It costs one 4x4 inverse per object per frame on the CPU,
instead of one per vertex on the GPU, and it is correct whatever the model
matrix does. (Our cube's face normals all lie along its own axes, which happen
to survive a non-uniform scale anyway, so you would not *see* the difference
here; on any curved or slanted mesh you would.)

## The Phong lighting model

The Phong model approximates the light leaving a surface as the sum of three
terms. Here is the classic picture at one point on the surface, with every
vector unit length and pointing *away* from the point:

```
              n
         l    ^    r
          \   |   /         l  direction to the light
           \  |  /     v    n  surface normal
            \ | /   .'      r  the light's reflection: l mirrored about n
             \|/ .'         v  direction to the viewer (the eye)
     =========+==========
```

**Ambient.** Light bounces around a real room so much that nothing is ever
completely black. Simulating that is expensive (it is what global illumination
techniques are about), so Phong fakes it with a small constant: every surface
gets a little of its own colour regardless of where the light is.

**Diffuse.** A matte surface scatters the light it catches equally in every
direction, so its brightness depends only on how much light *arrives*, and that
depends on the angle. Light hitting a surface head on is concentrated on a
small area; light at a glancing angle is spread over a larger one:

```
   head on: n . l = 1            glancing: n . l = 0.3       from behind: n . l < 0
       | | | |                       \ \ \ \                     (clamped to 0)
       v v v v                        \ \ \ \
   ===========                   ===========
```

That fall-off is exactly the cosine of the angle between `n` and `l`, which for
unit vectors is their **dot product**. Negative values mean the light is
behind the surface, so we clamp: `diffuse = max(dot(n, l), 0)`. This term is
most of what makes an object look three-dimensional.

**Specular.** A shiny surface also reflects some light mirror-like, which shows
up as a highlight where the reflected direction `r` points at your eye. The
closer `v` is to `r`, the brighter: `dot(v, r)`, clamped at zero. Raising it to
a power, the **shininess**, makes the highlight tighter: 2 is a broad sheen, 32
is plastic, 256 is a polished, almost pinpoint spot. Unlike the other two
terms, specular depends on where the viewer is, which is why the camera
position is a uniform.

```
  colour = ((ambient + diffuse) * objectColour + specular) * lightColour
```

The highlight is left uncoloured by the object (a white light on orange
plastic gives a white highlight), which is how plastics and paints behave.

### Phong or Blinn-Phong?

This chapter uses plain **Phong**, because its reflection vector matches the
picture above and the EDSL has `reflect()` ready. Most real-time renderers use
the **Blinn-Phong** variant, which replaces `dot(v, r)` with `dot(n, h)`, where
`h = normalize(l + v)` is the vector halfway between light and eye. It is
cheaper, and it avoids a Phong artefact: when `v` and `r` are more than 90
degrees apart the Phong term cuts off to exactly zero, which shows as a hard
edge on rough (low-shininess) surfaces. Switching is one of the exercises.

### Per vertex or per fragment?

You could evaluate all of this in the vertex shader and interpolate the
resulting colour (**Gouraud shading**), as EACP's `Teapot` example does. It is
cheaper, but a highlight that falls between vertices is lost or smeared. We
evaluate it in the fragment shader instead (**Phong shading**, confusingly
named after the same person), interpolating the world position and normal and
lighting every pixel. On our cube, whose faces are just two triangles each,
per-vertex lighting would barely show a highlight at all.

## Walking through the code

The vertex gains a normal:

```cpp
// Every vertex now carries a normal: the direction the surface faces at that
// point, of length 1, in the same local space as the position.
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};
```

The cube is now built by a short function rather than typed out. Each face is
its outward normal plus two axes across the face; the four corners are
`normal ± across1 ± across2`, halved so the cube is one unit across. Choosing
the axes so that `across1 x across2 = normal` (the cross product, by the
right-hand rule) is what makes the corners come out counter-clockwise seen
from outside, so back-face culling from the last chapter keeps working:

```cpp
namespace
{
struct CubeMesh
{
    std::array<Vertex, 24> vertices;
    std::array<std::uint16_t, 36> indices;
};

// A unit cube built face by face. Each face is described by its outward normal
// and two axes across it, chosen so that across1 x across2 = normal: walking
// the corners in the order below is then counter-clockwise seen from outside,
// which is front-facing. All four corners of a face share the face's normal,
// which is why the cube needs 24 vertices and not 8: a corner has three
// different normals, one per face it belongs to.
CubeMesh makeCube()
{
    struct Face
    {
        Vec3 normal, across1, across2;
    };

    const Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };

    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    auto mesh = CubeMesh {};

    for (auto f = 0; f < 6; ++f)
    {
        const auto& face = faces[f];

        for (auto c = 0; c < 4; ++c)
        {
            auto position = (face.normal + face.across1 * corners[c][0]
                             + face.across2 * corners[c][1])
                            * 0.5f;
            mesh.vertices[f * 4 + c] = {position, face.normal};
        }

        auto first = static_cast<std::uint16_t>(f * 4);
        const std::uint16_t quad[] = {0, 1, 2, 0, 2, 3};

        for (auto i = 0; i < 6; ++i)
            mesh.indices[f * 6 + i] = first + quad[i];
    }

    return mesh;
}
```

The lit shader is where everything in this chapter meets. In the vertex stage,
the position goes to world space with `model` and the normal with
`normalMatrix`, as a direction with `w = 0`. `.xyz()` is a **swizzle**: it
picks the first three components of the `Float4` back out. The clip position is
then the world position through `viewProjection`.

```cpp
// The lit object. The vertex stage moves positions and normals into world
// space, where the light and the camera are described; the fragment stage
// evaluates the lighting at every pixel.
struct LitShader final : ShaderProgram
{
    LitShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        // Positions are points (w = 1); normals are directions (w = 0), so the
        // translation in the matrix never moves them.
        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * float4(worldPosition, 1.0f));

        // Interpolated across each triangle. A blend of unit vectors is shorter
        // than 1, so the normal is renormalised per fragment.
        auto fragmentPosition = varying(worldPosition);
        auto n = normalize(varying(worldNormal));

        // The three directions Phong needs, all unit length, all pointing away
        // from the surface point: to the light, to the eye, and the light's
        // mirror reflection about the normal.
        auto l = normalize(lightPosition - fragmentPosition);
        auto v = normalize(cameraPosition - fragmentPosition);
        auto r = reflect(-l, n);

        auto ambient = 0.12f;
        auto diffuse = max(dot(n, l), 0.0f);
        auto specular = 0.6f * pow(max(dot(v, r), 0.0f), shininess);

        auto lit = (ambient + diffuse) * objectColor + specular;
        setFragment(float4(lit * lightColor, 1.0f));
    }

    static constexpr float shininess = 32.0f;

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> lightPosition;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> lightColor;
    Uniform<Float3> objectColor;

    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                lightPosition,
                cameraPosition,
                lightColor,
                objectColor)
};
```

Then the fragment stage. `varying(worldPosition)` and `varying(worldNormal)`
hand the two values to the rasterizer, which interpolates them across each
triangle, just as it did with colours in chapter 03 (Interpolation). The interpolated normal is
renormalised, because a blend of two unit vectors is usually a little shorter
than one. (On a flat cube face all the normals are equal and this changes
nothing, but it is a habit worth having.)

`l` and `v` point from the fragment to the light and to the eye.
`reflect(i, n)` mirrors an *incoming* direction about `n`, so we give it `-l`,
the direction the light travels, and get back `r` pointing away from the
surface. Then the three terms, exactly as in the formula. Everything here is
EDSL arithmetic recorded into the graph: `max(dot(n, l), 0.0f)` mixes a handle
with a float literal freely, `pow` takes the C++ constant `shininess` as a
literal, and adding a `Float` to a `Float3` broadcasts it across the three
components.

Notice which values are uniforms: the matrices, the light's position and colour,
the camera position and the object colour. They are the same for every vertex
and fragment in the draw, and `EACP_SHADER` lists them all so the compiler
knows the uniform block's layout.

The lamp is a second, much simpler program: the same cube vertices, an
unlit flat colour. It reads only the position from `Vertex`; a vertex field a
shader never asks for is just not used.

```cpp
// The lamp: the same cube, unlit, in the light's colour.
struct LampShader final : ShaderProgram
{
    LampShader() { compile(); }

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
} // namespace
```

The view prepares both programs with the depth-tested, back-face-culled
pipeline from the last chapter, and advances two angles:

```cpp
struct LightingView final : GPUView
{
    LightingView()
    {
        setDepth(true);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = true;
        pipeline.cullMode = CullMode::Back;

        // Both programs draw the same cube, each from its own copy of the
        // buffers, with the same depth-tested, back-face-culled pipeline.
        cubeShader.setVertices(cube.vertices);
        cubeShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        cubeShader.prepare(pipeline);

        lampShader.setVertices(cube.vertices);
        lampShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        lampShader.prepare(pipeline);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        lightAngle += 0.9f * delta;
        cubeAngle += 0.25f * delta;
    }
```

`render` computes the light's position on a circle around the cube, the
camera's matrices, and the cube's model matrix, then fills in the uniforms.
The normal matrix is `model.inverted().transposed()` on the CPU. The lamp's
model matrix is a tiny cube moved to the light's position, `T * S` as in
chapter 07 (Transformations). The two draws share one pass and one depth buffer, so the lamp is
correctly hidden when it passes behind the cube.

```cpp
    void render(Frame& frame) override
    {
        // The light circles the cube a little above it.
        auto lightPosition =
            Vec3 {1.6f * std::cos(lightAngle), 0.7f, 1.6f * std::sin(lightAngle)};
        auto lightColor = Vec3 {1.0f, 1.0f, 1.0f};
        auto cameraPosition = Vec3 {0.0f, 1.4f, 3.6f};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 0, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        auto model = Mat4::rotationY(cubeAngle) * Mat4::rotationX(0.5f);

        cubeShader.model = model;
        // The inverse-transpose keeps normals perpendicular to the surface even
        // under non-uniform scale. The shader language has no inverse(), so the
        // CPU, which does, computes it once per frame.
        cubeShader.normalMatrix = model.inverted().transposed();
        cubeShader.viewProjection = viewProjection;
        cubeShader.lightPosition = lightPosition;
        cubeShader.cameraPosition = cameraPosition;
        cubeShader.lightColor = lightColor;
        cubeShader.objectColor = Vec3 {1.0f, 0.5f, 0.31f};

        auto lampModel = Mat4::translation(lightPosition) * Mat4::scale(0.12f);
        lampShader.modelViewProjection = viewProjection * lampModel;
        lampShader.color = lightColor;

        auto pass = frame.beginPass({Graphics::Color {0.06f, 0.06f, 0.08f}});
        pass.draw(cubeShader);
        pass.draw(lampShader);
    }
```

## Try it yourself

1. Turn each term off in turn (set `ambient` to 0, or drop `diffuse` or
   `specular` from `lit`) and watch what each one contributes. *Hint: with no
   ambient, faces turned away from the light are pure black.*
2. Change `shininess` to 4 and then 256. *Hint: also try `0.6f` higher or lower
   for the specular strength; they are independent.*
3. Switch to Blinn-Phong: replace `r` with `h = normalize(l + v)` and use
   `dot(n, h)` in the specular term. *Hint: Blinn-Phong needs a higher
   shininess, about two to four times, for a similar-sized highlight.*
4. Make the light colour change over time, for instance
   `{1, 0.5 + 0.5 * sin(t), 0.5 + 0.5 * cos(t)}`, and pass the same colour to the
   lamp. *Hint: the cube's lit colour is the product of the two colours, so a
   blue light makes an orange object dark.*
5. Add `Mat4::scale({1.5f, 0.6f, 1.0f})` on the right of the cube's model
   matrix, then send `model` as the normal matrix too. Is it still lit
   correctly? Why, on this particular mesh? *Hint: re-read "Transforming
   normals" and consider which directions the cube's normals point in.*

## Further reading

- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has): the intrinsics (`normalize`, `dot`, `reflect`, `pow`), swizzles, and why there is no `inverse`.
- EACP GPU README, [The CPU types a shader value is fed from](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#the-cpu-types-a-shader-value-is-fed-from): a `Vertex` of `Vec3` position and normal, and a light direction sent as a `Uniform<Float3>`.
- `Apps/GPU/Teapot` in EACP: per-vertex (Gouraud) diffuse lighting of a curved mesh with real smooth normals.
- learnopengl.com, [Basic Lighting](https://learnopengl.com/Lighting/Basic-Lighting), and [Advanced Lighting](https://learnopengl.com/Advanced-Lighting/Advanced-Lighting) for Blinn-Phong.
- Song Ho Ahn's notes on [normal vector transformation](https://www.songho.ca/opengl/gl_normaltransform.html), for the derivation of the inverse transpose.

Previous: [08. Coordinate Systems](../08-CoordinateSystems/README.md) | Next: [10. Compute](../10-Compute/README.md)
