# 21. Normal Mapping

Since chapter 12 (Lighting Maps) our surfaces have had detail in their
*colour*: wood grain, steel, rivets. Their *shape* has stayed flat, though. A
textured brick wall that is really one flat quad looks fine in a photo, but
move a light across it and you see the trick at once: the whole wall brightens
and darkens as one piece, and the mortar grooves, which should catch shadow on
one side and light on the other, are only paint. In this chapter you will give
a flat wall bumpy lighting without adding a single triangle. A **normal map**
stores a surface direction for every texel. To use one you need **tangent
space**, a pair of extra vertex directions called the **tangent** and
**bitangent**, and the **TBN matrix** built from them. Along the way you will
see why EACP's top-left texture origin flips one channel of a normal map
compared with OpenGL tutorials.

## Why a flat wall looks flat

Recall from chapter 09 (Lighting) that diffuse and specular lighting depend
only on the surface normal `n` and on the directions to the light and the eye.
Our wall is two triangles, and every vertex carries the same normal, so every
pixel of the wall gets the same `n`. Whatever the diffuse map paints there, the
lighting can only vary smoothly with the light's direction and distance:

```
   what the wall is                       what the lighting sees
   (bricks standing out of mortar)        (one flat plane)

    ____      ____      ____               n   n   n   n   n   n
   |    |____|    |____|    |              ^   ^   ^   ^   ^   ^
   |    |    |    |    |    |              |   |   |   |   |   |
                                          ========================
```

The honest fix is geometry: model each brick's bevel and every groove with
real triangles. That is thousands of triangles for one wall. Normal mapping is
the cheap version. Keep the two triangles, but give the fragment shader a
different normal at every pixel, read from a texture. Lighting cannot tell the
difference between a surface that *is* tilted and one that only *reports* a
tilted normal. (The silhouette can: a normal-mapped wall seen edge on is still
perfectly flat. That limit is why it suits small bumps.)

```
   the same flat plane, with a normal per pixel

     n  n  n \  |  |  / n  n  n \  |  |  / n
     ^  ^  ^  \ |  | /  ^  ^  ^  \ |  | /  ^
    ===========================================
              ^ groove              ^ groove
```

## Storing directions in a texture

A unit normal has three components, each between -1 and 1. A texel has three
colour channels, each between 0 and 1. So the map stores every normal moved
into that range:

```
  stored = n * 0.5 + 0.5          (when making the map)
  n      = stored * 2.0 - 1.0     (when reading it in the shader)
```

A normal pointing straight out, (0, 0, 1), is stored as (0.5, 0.5, 1.0): the
light blue-violet that every normal map you have ever seen is mostly made of.
A texel tilted to the right gains red; tilted along the second axis, green.

Two details matter here. First, the texture must be a plain linear format,
`RGBA8Unorm`, and never an sRGB one: sRGB formats treat their values as colours
and bend them through a gamma curve when they are read, which would tilt every
normal. Second, 8 bits per channel and linear filtering both shorten the
vector a little, so the shader renormalises after unpacking.

## Tangent space

Which way is "right" for that red channel? Not world space: if the map stored
world directions, it would only work for a wall facing exactly one way, and
rotating the wall would break it. Instead, the map stores normals relative to
the surface itself, in **tangent space**:

- **z** points out of the surface, along the vertex normal **N**;
- **x** points the way the texture's **u** coordinate increases across the
  surface, the **tangent T**;
- **y** points the way the texture's **v** coordinate increases, the
  **bitangent B**.

```
            N (z: out of the surface)
            ^
            |
            |
            +--------> T (x: towards increasing u)
           /
          /
         v
        B (y: towards increasing v)
```

Because these axes are glued to the texture, one map works on any surface the
texture is put on, at any angle, on any object, and even wrapped around a
sphere. That is why tangent-space maps are the normal kind, and why they are
mostly blue: most of a surface points mostly "out".

### The TBN matrix

To light with a tangent-space normal we need it in the same space as the
light and the camera, world space, just as in chapter 09. If T, B and N are
written in world-space coordinates, then a tangent-space vector `(x, y, z)`
means "x of T, plus y of B, plus z of N":

```
  world = x * T + y * B + z * N  =  [ T  B  N ] * (x, y, z)
```

The matrix with T, B and N as its **columns** is the **TBN matrix**. In the
EDSL, `float3x3(t, b, n)` builds a `Float3x3` from three columns, and
multiplying it by a `Float3` does exactly the sum above.

### Where T and B come from

N comes with the mesh. T and B have to be worked out from what the mesh
already has, its positions and UVs. Take one triangle with corners `P0`, `P1`,
`P2` and texture coordinates `(u0, v0)`, `(u1, v1)`, `(u2, v2)`. Going along
its first edge, the position changes by `E1 = P1 - P0` while the UV changes by
`(du1, dv1)`; along its second edge, `E2` and `(du2, dv2)`.

```
                 P2 (u2, v2)
                 /\
                /  \
           E2  /    \
              /      \
             /________\
           P0    E1    P1 (u1, v1)
       (u0, v0)
```

T is how far the position moves per unit of u, and B per unit of v. Any step
across the triangle is some amount of each, so

```
  E1 = du1 * T + dv1 * B
  E2 = du2 * T + dv2 * B
```

That is two equations in the two unknown vectors T and B. Write them as a
2x2 matrix of UV differences times the pair (T, B) and invert the 2x2:

```
            1
  r = -------------------
      du1 * dv2 - du2 * dv1

  T = r * ( dv2 * E1 - dv1 * E2)
  B = r * (-du2 * E1 + du1 * E2)
```

If `du1 * dv2 - du2 * dv1` is zero, the triangle's UVs are squashed to a line
and it has no texture direction at all, so we skip it. A vertex shared by
several triangles adds up their answers. Finally the three vectors are made
**orthonormal** (unit length and at right angles) with **Gram-Schmidt**:
subtract from T its part along N, then subtract from B its parts along N and
T, and normalise each. On our flat wall they are perpendicular already; on a
curved mesh averaging can leave them slightly skewed, and the TBN matrix is
only a pure rotation when they are not.

## Which way is up: the green channel

Here EACP differs from OpenGL, and from learnopengl.com's normal mapping
chapter, in a way that matters.

In EACP, as in Metal and Direct3D, UV (0, 0) is the **top-left** texel and v
grows **downwards** through the image (chapter 06, Textures). In OpenGL, v grows
**upwards**. The bitangent is defined as the direction of increasing v, so on
our upright wall:

```
   EACP / Metal / D3D                      OpenGL

   (0,0) +-----> u                         v ^
         |                                   |
         |                                   |
       v v                           (0,0)  +-----> u

   B = dP/dv points DOWN the wall           B points UP the wall
```

The normal map's green channel is the normal's component along B. So the same
texel means "tilted up" in one convention and "tilted down" in the other. This
is the well-known split between "DirectX-style" (Y-, green down) and
"OpenGL-style" (Y+, green up) normal maps. A map made for the other convention
lights every horizontal edge from the wrong side: the top bevel of each brick
goes dark when the light is above it, and the grooves look like ridges. The
vertical edges still look right, which makes the mistake easy to miss.

In this chapter we make the map ourselves, so we simply write it in EACP's
convention: green is the component along +v, down the image. Everything stays
consistent: the bitangent computed from the UVs points the same way the map's
green axis does. If you ever load an OpenGL-style map instead, flip it in one
place: `tangentNormal.y = -tangentNormal.y` in the shader (or negate B, or
invert the green channel once when loading). Exercise 2 lets you see the wrong
version.

## World space or tangent space?

There are two ways to bring the light and the normal together:

1. **Normal to world space.** Per fragment, multiply the map's normal by TBN
   and light in world space exactly as in chapter 09. Simple, and the lighting
   code does not change at all.
2. **Light and eye to tangent space.** Because T, B and N are orthonormal, the
   inverse of TBN is its transpose. Per vertex, multiply the light position,
   eye position and fragment position by `transpose(TBN)` and pass *those* to
   the fragment stage, which then lights directly with the unpacked map
   normal and no matrix at all. This moves the matrix work from every fragment
   to every vertex, which is why many engines prefer it.

We use the first: it keeps the lighting identical to the chapters before, so
the only new thing on screen is the normal. Converting to the second is
exercise 4.

## Making the maps

Both textures come from one description of the wall, a **height function**:
how far the surface stands out at each texel. It is 0 deep in the mortar,
rises over a rounded bevel near each brick edge, and is about 1 on a brick's
face, with a little noise on top so the bricks are not perfectly smooth. The
noise is chapter 12's value noise.

```
  height along one row of texels

  1 |      .--------.         .--------.
    |     /          \       /          \
    |    /            \     /            \
  0 |___/              \___/              \___
       mortar  brick   mortar   brick    mortar
```

A normal is perpendicular to the surface, so it tilts *against* the slope:
where the height rises to the right, the surface faces left. With the slope
measured in each texture direction by **central differences**, the
difference between the texel after and the texel before,

```
  dh/du = (h(x+1, y) - h(x-1, y)) / 2
  dh/dv = (h(x, y+1) - h(x, y-1)) / 2

  n = normalize(-strength * dh/du, -strength * dh/dv, 1)
```

where `strength` scales how steep the bumps look. That `n` is already in
tangent space (x along u, y along v, z out of the wall), so it is packed into
RGB and written out. The diffuse map is made in the same loop, from the same
brick layout, so the colour and the bumps line up texel for texel.

## Walking through the code

The vertex gains the two texture directions:

```cpp
// Chapter 12's vertex, plus the two directions a normal map needs: which way
// the texture's u and v axes run across the surface at this vertex.
struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec3 tangent; // the direction of increasing u (to the right in the texture)
    Vec3 bitangent; // the direction of increasing v (down the texture)
};
```

Meshes are now built from faces with chapter 09's face table, into growable
vectors, so the same `addFace` makes both the one-face wall and the six-face
lamp cube. It leaves the tangents empty. `computeTangents` fills them in for
any mesh, using exactly the algebra above:

```cpp
void computeTangents(Mesh& mesh)
{
    auto tangents = std::vector<Vec3>(mesh.vertices.size());
    auto bitangents = std::vector<Vec3>(mesh.vertices.size());

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        // ... the triangle's corners v0, v1, v2 ...
        auto e1 = v1.position - v0.position;
        auto e2 = v2.position - v0.position;
        auto du1 = v1.uv.x - v0.uv.x;
        auto dv1 = v1.uv.y - v0.uv.y;
        auto du2 = v2.uv.x - v0.uv.x;
        auto dv2 = v2.uv.y - v0.uv.y;

        auto determinant = du1 * dv2 - du2 * dv1;

        if (std::abs(determinant) < 1e-8f)
            continue; // degenerate UVs: this triangle has no texture direction

        auto r = 1.0f / determinant;
        auto t = (e1 * dv2 - e2 * dv1) * r;
        auto b = (e2 * du1 - e1 * du2) * r;

        // ... added to each of the three corners ...
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        auto& vertex = mesh.vertices[i];
        const auto n = vertex.normal;

        // Remove any part of T along N, then any part of B along N and T.
        auto t = normalize(tangents[i] - n * dot(n, tangents[i]));
        auto b = bitangents[i] - n * dot(n, bitangents[i]);
        b = normalize(b - t * dot(t, b));

        vertex.tangent = t;
        vertex.bitangent = b;
    }
}
```

On the wall, u runs left to right and v top to bottom, so this produces
T = +x and B = -y: the bitangent points down the wall, as promised.

The maps are generated in `makeWallMaps`. `brickAt` says which brick a texel
belongs to and how far it is from the nearest mortar line, with odd rows
shifted half a brick; `heightAt` turns that into a height. The heart of the
normal map is four height lookups per texel:

```cpp
auto dhdu = (heightAt(x + 1, y) - heightAt(x - 1, y)) * 0.5f;
auto dhdv = (heightAt(x, y + 1) - heightAt(x, y - 1)) * 0.5f;
auto n = normalize(Vec3 {-strength * dhdu, -strength * dhdv, 1.0f});

// A unit vector's components are in -1..1; a texel's in 0..1.
auto* m = &normals[(y * mapSize + x) * 4];
m[0] = toByte(n.x * 0.5f + 0.5f);
m[1] = toByte(n.y * 0.5f + 0.5f);
m[2] = toByte(n.z * 0.5f + 0.5f);
```

`heightAt` wraps its coordinates around the texture, so the texels on the
edges take their slopes from the opposite edge and the map tiles without a
seam.

In the shader's vertex stage, T and B go to world space with the **model**
matrix, and N with the normal matrix. The difference is the one chapter 09
made: T and B lie *in* the surface, like a triangle's edges, so they stretch
and turn exactly as the surface does; the normal is perpendicular to it and
needs the inverse transpose.

```cpp
auto worldTangent = (model * float4(tangent, 0.0f)).xyz();
auto worldBitangent = (model * float4(bitangent, 0.0f)).xyz();
auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();
```

In the fragment stage, the three interpolated directions are renormalised and
become the TBN matrix's columns. The texel is unpacked from 0..1 back to
-1..1 and turned into a world-space normal:

```cpp
auto t = normalize(varying(worldTangent));
auto b = normalize(varying(worldBitangent));
auto flatNormal = normalize(varying(worldNormal));

auto tbn = float3x3(t, b, flatNormal);

// Unpack the texel from 0..1 back to a -1..1 direction.
auto tangentNormal = sample(normalMap, uv).xyz() * 2.0f - 1.0f;
auto mappedNormal = normalize(tbn * tangentNormal);
```

`Float3x3` is a shader-local type: you can build and use one inside
`define()`, but not send one as a uniform (the GPU README explains why; send a
`Float4x4` instead). Here it never leaves the fragment stage.

Then the split. To compare the two normals side by side, the shader needs to
know where on screen each pixel is. The clip-space position is a value like
any other, so we pass its x and w through as varyings and divide, getting the
pixel's x in normalized device coordinates (chapter 08): -1 at the left of the
window, +1 at the right. Pixels left of the `split` uniform use the flat
normal, the rest the mapped one, and a thin line marks the boundary:

```cpp
auto ndcX = varying(clipPosition.x()) / varying(clipPosition.w());
auto n = select(ndcX < split, flatNormal, mappedNormal);
```

`select(condition, a, b)` is the EDSL's `condition ? a : b`, and works on
whole vectors. Both normals are computed for every pixel and one is picked;
there is no branch. Everything after this line is chapter 09's Phong lighting,
with the diffuse map's colour as the object colour and a simple fall-off with
distance (chapter 13's attenuation, with a constant of 1 and a quadratic of
0.2), so the lit patch follows the lamp across the wall.

The view is chapter 09's, with two small additions. The wall turns gently from
side to side, to show that the tangent frame turns with the model matrix and
the bumps stay lit correctly at any angle. And Space cycles the `split`
uniform between 0 (split down the middle), 2 (everything left of it, so all
flat) and -2 (all normal-mapped):

```cpp
void keyDown(const Graphics::KeyEvent& event) override
{
    if (event.keyCode != Graphics::KeyCode::Space || event.modifiers.command)
    {
        passKeyOn();
        return;
    }

    mode = (mode + 1) % 3;
}
```

The light circles close in front of the wall, at half a unit, so that it
rakes across the bricks at a low angle. A light shining straight at a bumpy
surface barely reveals the bumps; a low, glancing light exaggerates them.
That holds for real walls too.

## What you should see

A brick wall turning slowly from side to side, split down the middle by a thin
bright line, with a small warm lamp circling in front of it. On the left the
wall is a photograph: the lamp makes a soft round glow that slides over
bricks and mortar alike, and nothing about it suggests depth. On the right the
same bricks stand out of the wall. Each brick's bevel facing the lamp catches
the light and the bevel facing away falls into shadow, the mortar sits in dark
grooves, and the bricks' faces show a fine grain. As the lamp circles, the
lit edges move around each brick with it: when the lamp is to the left the
left bevels shine, when it is above, the top ones. Press Space to see the wall
all flat, and again for all normal-mapped.

## Try it yourself

1. Change `strength` in `makeWallMaps` to 2 and then 20. *Hint: too much and
   the bevels look like sharp creases, and the shading starts to contradict
   the flat silhouette at the wall's edges.*
2. Flip the convention: write `-n.y` into the green channel, as an
   OpenGL-style map would have it. Watch a brick while the lamp is above it.
   *Hint: the vertical bevels still look right; only the horizontal ones
   invert, which is exactly what makes this bug easy to miss.*
3. Turn the wall into a cube: build it from all six faces with
   `addFace(mesh, ..., 0.5f, 0.5f)` as `makeCube` does, and call
   `computeTangents`. *Hint: every face gets its own T and B from its UVs, so
   the bricks light correctly on all six without any other change.*
4. Light in tangent space instead. In the vertex stage build
   `transpose(float3x3(t, b, n))` from the world-space directions, use it to
   take `lightPosition`, `cameraPosition` and `worldPosition` into tangent
   space, and pass those as varyings. In the fragment stage, light with the
   unpacked map normal directly. *Hint: the picture should not change at all;
   only where the work happens does.*
5. Show the maps themselves: set the fragment colour to
   `sample(normalMap, uv).xyz()`, then to `mappedNormal * 0.5f + 0.5f`.
   *Hint: the first is the classic blue-violet picture; the second changes
   colour as the wall turns, because it is in world space.*
6. **Parallax mapping.** A normal map changes the lighting but not *where*
   things appear: a groove never hides part of the brick behind it.
   Parallax mapping shifts the UV a little before sampling, according to the
   height and the view direction. Store the height in the normal map's alpha
   channel, take the view direction into tangent space (as in exercise 4), and
   sample at `uv - viewTangent.xy() / viewTangent.z() * height * 0.03f`.
   *Hint: the minus sign and which height counts as "deep" depend on your
   conventions; if the bricks seem to sink rather than stand out, flip one.
   learnopengl's parallax chapter takes this much further.*

## Further reading

- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has): `Float3x3` built from columns, `transpose`, `select`, and why there is no `inverse`.
- EACP GPU README, [What it deliberately refuses](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-it-deliberately-refuses): why a `Float3x3` cannot be a uniform.
- learnopengl.com, [Normal Mapping](https://learnopengl.com/Advanced-Lighting/Normal-Mapping) and [Parallax Mapping](https://learnopengl.com/Advanced-Lighting/Parallax-Mapping). Remember that their bitangent and green channel point the other way.
- Eric Lengyel's [derivation of tangent space for an arbitrary mesh](https://terathon.com/blog/tangent-space.html), the source of the per-triangle formula above.
- [MikkTSpace](http://www.mikktspace.com/), the tangent-space standard most modelling tools bake their normal maps against.

Previous: [20. Shadow Mapping](../20-ShadowMapping/README.md) | Next: [22. HDR and Bloom](../22-HDRBloom/README.md)
