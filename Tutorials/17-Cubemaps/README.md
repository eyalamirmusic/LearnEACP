# 17. Cubemaps

Every scene so far has floated in a flat clear colour. In this chapter you will
put a whole world around it, a sky with clouds, a sun, a ring of mountains and
a chequered plain stretching to the horizon, and then put two objects in it
that *show* that world: a mirror cube that reflects it and a glass sphere that
bends it. All three come from one new kind of texture, the **cube map**: six
square images that together cover every direction. You will learn how a cube
map is read with a direction rather than a coordinate, how its six faces are
laid out, how to draw a **skybox** that always stays infinitely far away, and
how `reflect()` and `refract()` turn a cube map into a cheap but convincing
**environment map**.

There are no image files in this series, so the sky is painted procedurally,
one texel at a time, on the CPU. That turns out to be a good way to understand
cube maps: to paint a texel you have to know exactly which direction it stands
for.

## A texture you read with a direction

A 2D texture, as in chapter 06 (Textures), is read by *position*: you hand
`sample` a UV coordinate and get back the texel there. A cube map is read by
*direction*. Imagine standing at the centre of a cube whose six inside walls
are painted with what you would see in each direction. To look something up,
you point a ray out of the centre; the wall it hits, and the spot where it hits
it, is the answer.

```
              +Y
              |   ray (0.4, 0.7, -0.6)
              |  /
              | /         the largest component (0.7, on y) picks the face: +Y.
              |/          the other two, divided by it, give the spot on that
     ---------+--------- +X                     face: (0.4 / 0.7, -0.6 / 0.7)
             /
            /
          +Z
```

The GPU does this in hardware:

1. Find the component of the direction with the largest absolute value. Its
   axis and sign choose the face: +X, -X, +Y, -Y, +Z or -Z.
2. Divide the other two components by it. That gives two numbers `s` and `t`
   in -1..1, a position on the face.
3. Remap them to 0..1 and sample that face like a 2D texture.

Two things follow. First, the **length of the direction does not matter**: the
division in step 2 cancels it, so you never have to normalise a vector you use
only for a cube map lookup. Second, there are no edges to run off. Every
direction lands on some face, and directions just either side of a seam land on
neighbouring faces at matching texels, so a correctly built cube map has no
visible seams at all. That is why the address mode hardly matters for a cube
map; we use clamp, and the hardware handles the joins.

In the shader EDSL a cube map is a `Uniform<TextureCube>`, and you read it with
the same `sample` as a 2D texture, giving it a `Float3` instead of a `Float2`.

## Six faces, in a fixed order and orientation

On the CPU a cube map is one block of pixels: six square faces one after
another, in the order **+X, -X, +Y, -Y, +Z, -Z**, each stored row by row from
the top, exactly like six 2D textures laid end to end. EACP uploads it in one
call, with `cube = true` in the `TextureDescriptor`.

The order is the easy part. The hard part is which way round each face goes:
for every face, which direction does "right" (increasing u) point, and which
does "down" (increasing v, row 0 at the top)? Every graphics API agrees on the
same convention, inherited from RenderMan. With `s = 2u - 1` and `t = 2v - 1`,
the texel at (u, v) of each face stands for this direction:

| face | direction for (s, t)  | u grows towards | v grows towards |
| ---- | --------------------- | --------------- | --------------- |
| +X   | ( 1, -t, -s)          | -Z              | -Y (down)       |
| -X   | (-1, -t,  s)          | +Z              | -Y (down)       |
| +Y   | ( s,  1,  t)          | +X              | +Z              |
| -Y   | ( s, -1, -t)          | +X              | -Z              |
| +Z   | ( s, -t,  1)          | +X              | -Y (down)       |
| -Z   | (-s, -t, -1)          | -X              | -Y (down)       |

This table is just the GPU's lookup run backwards. Get a row wrong and the
picture still looks like a sky, but one face is mirrored or turned, and its
edges no longer meet its neighbours': the horizon breaks, the ground grid
kinks at the seam, or the sun appears in the wrong place. That is one reason
our sky has a grid on the ground and mountains on the horizon, both of which
cross every vertical seam. If they run straight across, the faces are right.

You will sometimes read that cube map faces look "mirrored", because they are
viewed from the inside. That matters when you load six photographs someone else
took. We sidestep the question entirely: we work out each texel's direction
from the table and ask what the world looks like in that direction, so whatever
direction the GPU looks up later, it gets the colour we meant for it.

## Painting a sky as a function of direction

Since a cube map answers "what is in direction d?", the natural way to fill one
is a function from a direction to a colour. Ours, `skyColor`, layers a few
simple ideas:

- **Gradient.** Above the horizon, blend from a pale horizon colour to a deep
  blue as `d.y` rises.
- **Clouds.** Imagine a flat ceiling one unit above the eye. The ray `d` meets
  it at `(d.x / d.y, d.z / d.y)`, so we paint noise on that ceiling. Clouds near
  the horizon come out squashed and far away, as real ones do, for free.
- **Mountains.** A skyline whose height is a sum of sine waves of the compass
  direction (`atan2(d.z, d.x)`), drawn wherever the ray's elevation is below it.
- **Sun.** A glow that grows with `dot(d, sunDirection)`, and a white disc where
  that dot product is very close to 1.
- **Ground.** Below the horizon, the same ceiling trick on a floor one unit
  below the eye: a chequerboard with grid lines, fading into haze in the
  distance.

None of this knows how far away anything is, and it does not need to: a cube
map stores only directions. Everything in it behaves as if it were infinitely
far away, which is exactly right for a sky and, as we will see, the main
limitation of environment mapping.

## The skybox

To draw the sky we put a cube around the camera and, for every pixel of it,
sample the cube map in the direction from the camera to that pixel. Three
details make it work.

### It moves with the camera

A sky is infinitely far away, so walking forwards must not bring it any closer.
Only *turning* the camera should change what you see. So the skybox gets a view
matrix with the **translation removed**: the same rotation as the real view
matrix, but no movement. The camera is then always at the centre of the box.

`Mat4` is column-major, and its translation lives in the fourth column, so
removing it is one line on the CPU:

```cpp
auto skyView = view;
skyView.setColumn(3, {0.0f, 0.0f, 0.0f, 1.0f});
```

With the camera always at the centre, the box's local position is also the
direction from the camera to that point on the box. That is the direction we
sample with, so the vertex shader just passes the position along as a varying.
(An OpenGL tutorial would build a 3x3 matrix from the 4x4 and back; zeroing the
column is the same thing.)

### It is always at the far plane

The box reaches only one unit from the camera, so if we let it be depth-tested normally it
would be in front of anything more than a unit from the camera. We want the
opposite: the sky should be behind *everything*. The trick is in the vertex
shader. After the projection, the GPU divides z by w to get the depth. If we
write w into z, the depth comes out as `w / w = 1.0`, the far plane, whatever
the vertex's real distance:

```cpp
setPosition(clip.xyww());
```

`xyww` is a swizzle like the `xyz` of chapter 09: it builds a `Float4` from the
x, y, w and w components.

### Drawn last, with less-or-equal

The depth buffer is cleared to 1.0 every frame, and every sky pixel has depth
exactly 1.0. So:

- with the **default `LessEqual`** test, a sky pixel passes wherever nothing has
  been drawn yet (1.0 <= 1.0), and fails wherever an object is in front of it
  (1.0 > the object's depth). With `Less` it would never pass at all, and the
  whole sky would vanish.
- it does not need to **write** depth, since nothing is drawn after it, so we
  set `depthWrite = false`.

Drawing the sky **last** means its expensive fragment work (a texture lookup per
pixel) is only done for the pixels that actually show sky; the depth test
throws away the rest before the fragment shader runs.

The other classic approach, from the days before this trick, is to draw the
sky **first** with depth writes off: it paints the whole screen, leaves the
depth buffer at 1.0, and everything drawn afterwards simply covers it. That
also works, and does not need the `xyww` trick, but it shades every pixel of
the screen with the sky even if an object later covers most of it. Drawing last
saves that work, which is why it is the usual choice.

### Seen from the inside

Our skybox cube is wound like every other closed mesh: counter-clockwise seen
from *outside*. But the camera is inside it, looking at every face from behind,
so all of its faces appear clockwise. Rather than reversing the indices, we tell
the pipeline that for this mesh clockwise is front: `frontFace =
Winding::Clockwise`, and back-face culling then culls nothing we can see. (With
`CullMode::None` it would draw the same picture; culling just makes the intent
explicit.)

## Environment mapping: reflection

A mirror shows you what you would see if your view ray bounced off it. In the
fragment shader we know the surface position and normal, and the camera
position, so we can work out that bounced ray and look it up in the sky:

```
           n
           ^
     I     |     R          I  the view ray: from the eye to the surface
      \    |    /           n  the surface normal (unit length)
       \   |   /            R  reflect(I, n) = I - 2 dot(I, n) n
        \  |  /
         v | /
   ========+========
```

`reflect(I, n)` is the same intrinsic chapter 09 used for the specular
highlight; there it mirrored the light direction, here it mirrors the view
direction. Note that `I` points *from the eye towards the surface*, the way the
light actually travels, so it is `surface - cameraPosition`, not the other way
round. `n` must be unit length for `reflect` to be a true mirroring, which is
why the interpolated normal is renormalised.

This is **environment mapping**, and it is a cheat in two ways worth knowing.
The cube map stores directions only, so a reflection is treated as if
everything it shows were infinitely far away: our mirror cube reflects the
sky, but not the glass sphere sitting right beside it. And the cube map never
changes, so a moving object would not show up in it either. Real-time engines
fix both by rendering the scene *into* a cube map from the object's position,
six views, one per face, which is a **dynamic environment map** (see the
exercises).

## Environment mapping: refraction

Light passing from one material into another bends. How much depends on each
material's **index of refraction** (IOR):

| material | IOR  |
| -------- | ---- |
| air      | 1.00 |
| water    | 1.33 |
| ice      | 1.31 |
| glass    | 1.52 |
| diamond  | 2.42 |

`refract(I, n, eta)` bends the incoming direction `I` at a surface with normal
`n`, where `eta` is the **ratio** of the two indices: the one the ray leaves
divided by the one it enters. Going from air into glass that is
`1.00 / 1.52`, about 0.66. A ratio below 1 bends the ray towards the normal,
into the material:

```
          n
          ^       I
          |      /         air (1.00)
          |    /
   =======+=========== surface
         /|
        / |                glass (1.52): the ray bends towards -n
       T  |
```

Like `reflect`, it wants both vectors unit length. (Going the other way, from a
denser material into a lighter one at a steep enough angle, there is no
refracted ray at all, **total internal reflection**, and `refract` returns the
zero vector.)

We refract only once, at the front surface, and look the bent ray up in the
sky. A real glass ball refracts twice, entering and leaving, and real glass
also reflects a little, more at glancing angles (the Fresnel effect). The
single bend is the standard cheap approximation, and it already gets the most
striking thing right: a glass ball turns the world upside down, ground at the
top and sky at the bottom.

## Walking through the code

The vertex for the two objects is chapter 09's, position plus normal. The
skybox needs only a position, which doubles as its sampling direction:

```cpp
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

// The skybox only needs a position: it is also the direction the cube map is
// sampled with.
struct SkyVertex
{
    Vec3 position;
};
```

### Building the cube map

`skyColor(d)` is the sky painting described above; read it in `Main.cpp`, it
is plain C++ maths with no graphics API in it. The part that is about cube maps
is `faceDirection`, the table from earlier as code:

```cpp
Vec3 faceDirection(int face, float u, float v)
{
    auto s = 2.0f * u - 1.0f;
    auto t = 2.0f * v - 1.0f;

    switch (face)
    {
        case 0:
            return {1.0f, -t, -s}; // +X
        case 1:
            return {-1.0f, -t, s}; // -X
        case 2:
            return {s, 1.0f, t}; // +Y
        case 3:
            return {s, -1.0f, -t}; // -Y
        case 4:
            return {s, -t, 1.0f}; // +Z
        default:
            return {-s, -t, -1.0f}; // -Z
    }
}
```

`makeSkyCubeMap` walks the six faces in order, row by row, and for each texel
asks `skyColor` about the texel's direction. It takes four samples spread over
each texel and averages them, so the hard edges in the sky (grid lines, the
ridge of the mountains, the rim of the sun) come out smooth instead of
stair-stepped. The faces are 512 texels square: each face covers 90 degrees of
view, so a much smaller face looks blurry once a 50-degree camera stretches it
across the window.

```cpp
    for (auto face = 0; face < 6; ++face)
        for (auto y = 0; y < faceSize; ++y)
            for (auto x = 0; x < faceSize; ++x)
            {
                // Four samples spread over the texel, averaged, so the
                // sharp edges (grid lines, mountains, the sun) are smooth.
                auto color = Vec3 {};

                for (auto sample = 0; sample < 4; ++sample)
                {
                    auto u = (x + 0.25f + 0.5f * (sample % 2)) / faceSize;
                    auto v = (y + 0.25f + 0.5f * (sample / 2)) / faceSize;
                    color = color + skyColor(normalize(faceDirection(face, u, v)));
                }

                color = color * 0.25f;

                *texel++ = toByte(color.x);
                *texel++ = toByte(color.y);
                *texel++ = toByte(color.z);
                *texel++ = 255;
            }

    auto descriptor = TextureDescriptor {};
    descriptor.width = faceSize;
    descriptor.height = faceSize;
    descriptor.format = TextureFormat::RGBA8Unorm;
    descriptor.cube = true;
    // Curved mirrors squeeze a lot of sky into a few pixels; mipmaps keep that
    // from shimmering.
    descriptor.mipmapped = true;

    return Device::shared().makeTexture(descriptor, pixels.data());
```

`width` and `height` are the size of *one face*; `cube = true` tells the
device the pixel block holds six of them. `mipmapped` builds a chain of
smaller versions of every face. The edge of the glass sphere squeezes a large
part of the sky into a handful of pixels, and without mipmaps those pixels
would flicker between unrelated texels as the camera moves.

### The meshes

The cube is chapter 09's `makeCube`, now filling `std::vector`s. The sphere is
new: rings of latitude and segments of longitude, with the normal equal to the
position, because on a unit sphere centred at the origin they are the same
vector. Neighbouring quads share vertices, so the normals blend smoothly across
the surface and the reflections and refractions curve smoothly with them.

```cpp
            auto normal = Vec3 {std::sin(polar) * std::cos(azimuth),
                                std::cos(polar),
                                std::sin(polar) * std::sin(azimuth)};
            mesh.vertices.push_back({normal, normal});
```

The skybox is the eight corners of a cube from -1 to 1 and 36 indices, wound
counter-clockwise from outside.

### The skybox shader

```cpp
struct SkyboxShader final : ShaderProgram
{
    SkyboxShader()
    {
        // Sampler state is fixed at compile(). Clamp is the only sensible
        // address mode on a cube map: the hardware handles the seams between
        // faces itself.
        sky.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&SkyVertex::position);
        auto clip = viewProjection * float4(position, 1.0f);

        // Replace z with w, so that after the divide by w the depth is exactly
        // 1.0: the far plane. Every pixel of the sky is behind everything else.
        setPosition(clip.xyww());

        // The corner of the box is also the direction from the camera to it,
        // and a cube map is sampled by direction. The length does not matter.
        setFragment(sample(sky, varying(position)));
    }

    Uniform<Float4x4> viewProjection;
    Uniform<TextureCube> sky;

    EACP_SHADER(viewProjection, sky)
};
```

As with a `Uniform<Texture2D>`, the sampling (linear filtering, clamp) is set
before `compile()`, because it is baked into the compiled shader. The
`viewProjection` it receives is `projection * skyView`, the projection times
the view matrix with its translation removed. The interpolated position,
`varying(position)`, is the direction from the camera to the pixel on the box;
it goes straight into `sample`, unnormalised.

### The mirror and the glass

The two object shaders share the lit shader's vertex stage from chapter 09:
position to world space with `model`, normal with `normalMatrix`, clip position
through `viewProjection`. Only the last three lines differ. The mirror:

```cpp
        // incident points from the eye to the surface: the direction the view
        // ray travels. reflect() mirrors it about the normal.
        auto incident = normalize(varying(worldPosition) - cameraPosition);
        auto n = normalize(varying(worldNormal));
        auto reflected = reflect(incident, n);

        setFragment(sample(sky, reflected));
```

The glass, with `ratio = 1.00f / 1.52f` as a C++ constant the EDSL takes as a
literal, and a faint tint because thick glass is never perfectly clear:

```cpp
        auto incident = normalize(varying(worldPosition) - cameraPosition);
        auto n = normalize(varying(worldNormal));
        auto refracted = refract(incident, n, ratio);

        // A faint green-blue tint, as thick glass has.
        auto seen = sample(sky, refracted).xyz() * tint;
        setFragment(float4(seen, 1.0f));
```

Both normalise `incident` even though a cube map lookup would not care about
its length: `reflect` and `refract` assume unit vectors (for `reflect` it only
scales the result, for `refract` the angle would come out wrong). There is no
lighting at all. A perfect mirror or a clear glass has no colour of its own;
everything you see on it is the environment.

### The pipelines and the draw order

The objects use the depth-tested, back-face-culled pipeline of chapter 08. The
sky has its own, spelled out field by field:

```cpp
        auto skyPipeline = RenderPipelineDescriptor {};
        skyPipeline.sampleCount = sampleCount();
        skyPipeline.depth = true;
        skyPipeline.depthCompare = DepthCompare::LessEqual;
        skyPipeline.depthWrite = false;
        skyPipeline.cullMode = CullMode::Back;
        skyPipeline.frontFace = Winding::Clockwise;
```

`LessEqual` is the default; it is written out here because the skybox depends
on it. Every field maps onto one of the skybox details above. Each of the three
shaders gets the same texture with `shader.sky = skyTexture`. That stores a
pointer, so the `Texture` is declared as a member before the shaders and lives
as long as they do.

`render` builds an orbiting camera from three numbers, yaw, pitch and
distance, which the mouse changes: drag to turn around the scene, scroll to
move in and out. (As in chapter 11, the constructor calls
`setHandlesMouseEvents(true)`; without it the view never receives the mouse.)
When you are not dragging, the yaw creeps forwards on its own
so the camera slowly circles. Then it fills in the uniforms and draws the
mirror, the glass and, last, the sky:

```cpp
        auto pass = frame.beginPass({Graphics::Color {0.0f, 0.0f, 0.0f}});
        pass.draw(mirror);
        pass.draw(glass);
        pass.draw(skybox);
```

The clear colour is black, and you never see it: the sky fills every pixel the
objects leave.

## What you should see

A wide landscape: blue sky with soft clouds, the sun up and to one side with a
glow around it, grey mountains with snowy peaks along the horizon, and green
chequered fields running off into haze. On the left, a slowly turning mirror
cube; each of its flat faces shows a sharp, undistorted piece of the sky and
mountains from behind and around the camera, and the picture jumps at every
edge of the cube, where the normal does. On the right, a glass sphere shows the
world upside down and squeezed towards its rim: chequered ground across the
top, the mountains and sky below. As the camera circles, the sky turns
smoothly with no visible seams, the ground grid and the mountain ridge run
straight across the face boundaries, and the sun passes behind and through the
objects' reflections.

## Try it yourself

1. Change the sky's depth test to `DepthCompare::Less`. *Hint: the sky's depth
   is exactly equal to the cleared depth, so it fails everywhere.*
2. Draw the sky first instead: move `pass.draw(skybox)` to before the objects
   and remove the `xyww` (use `setPosition(clip)`). Does the picture change?
   What would happen if the sky still wrote depth? *Hint: a box only one unit from
   the camera is nearer than most of the scene.*
3. Leave the translation in the sky's view matrix (use `projection * view`
   for the skybox). *Hint: the camera is now outside the two-unit box, so the
   sky shrinks to a small cube around the origin, and with the clockwise front
   face you see its far inside walls. The rest of the screen is the black clear
   colour.*
4. Try other materials in the glass shader: water (`1.00f / 1.33f`) and
   diamond (`1.00f / 2.42f`). Then swap the shaders over, so the cube refracts
   and the sphere reflects. *Hint: a flat refracting face barely distorts
   anything, which is why glass windows are flat.*
5. Add a Fresnel term to the glass: sample both the reflected and the refracted
   direction and blend them with `mix(refracted, reflected, f)`, where
   `f = 0.04 + 0.96 * pow(1 - max(dot(-incident, n), 0), 5)` (Schlick's
   approximation). *Hint: the rim of the sphere should turn into a mirror.*
6. Move the sun (`sunDirection`) to just above the horizon and make the sky
   colours warmer for a sunset. *Hint: everything is baked once at start-up,
   so a sky that changes over time would need the cube map rebuilt or
   rendered on the GPU; render-to-texture into each face is how a dynamic
   environment map works (see chapter 16, Framebuffers).*

## Further reading

- EACP GPU README, [Arrays, cube arrays and volumes](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#arrays-cube-arrays-and-volumes): `TextureType::Cube`, cube arrays, slices and face order.
- EACP GPU README, [Pipeline state](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#pipeline-state): `depthCompare`, `depthWrite`, and `frontFace` for an inside-out skybox.
- `Apps/GPU/CubeMap` in EACP: a sky and a mirror ball, with faces built to make a mistake in the face order or orientation visible, and a check that reads the result back.
- learnopengl.com, [Cubemaps](https://learnopengl.com/Advanced-OpenGL/Cubemaps).
- The OpenGL specification's table of cube map face selection (section 8.13, "Cube Map Texture Selection"), the source of the face table above.
- NVIDIA, *GPU Gems*, chapter 19, [Image-Based Lighting](https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-19-image-based-lighting), for reflections that account for the environment's distance.

Previous: [16. Framebuffers](../16-Framebuffers/README.md) | Next: [18. Instancing](../18-Instancing/README.md)
