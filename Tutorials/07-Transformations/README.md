# 07. Transformations

So far every vertex has been drawn exactly where its numbers say, and the only
motion you have seen was a little `cos`/`sin` arithmetic written by hand in a
vertex shader. That stops scaling the moment you want to move, turn *and*
resize something, or stack one motion on top of another. In this chapter we
build the tool every 3D program uses for the job: the **4x4 transformation
matrix**. You will draw a small quad that spins on its own centre while it
orbits the middle of the window, and the whole of that motion will reach the
GPU as sixteen floats. By the end you will know what vectors and matrices are
(enough for graphics, anyway), why graphics uses 4x4 matrices for 3D and even
for 2D, how to write scale, rotation and translation as matrices, and why the
order you multiply them in changes the picture.

## Vectors

A **vector** is a list of numbers that together describe one thing. In
graphics that thing is usually either a *position* ("the point at x = 0.5,
y = -0.5") or a *direction* ("pointing up and to the right"). A 2D vector has
two components, `(x, y)`; a 3D vector has three, `(x, y, z)`. You have been
using them since chapter 02 (Hello Triangle): `Maths::Vec2` and `Maths::Vec3` on the CPU, and
`Float2`/`Float3`/`Float4` in the shader.

```
      y
      ^
      |      * (0.5, 0.5)       a position: a place
      |     /
      |    /                    a direction: an arrow, which does not care
      |   /                     where it starts
      +--------------> x
```

The operations we need are few:

- **Adding** two vectors adds their components:
  `(1, 2) + (3, 1) = (4, 3)`. Adding a direction to a point moves the point.
- **Scaling** a vector multiplies every component by a number:
  `2 * (1, 2) = (2, 4)`.
- The **length** of `(x, y)` is `sqrt(x*x + y*y)`. A vector of length 1 is a
  *unit* vector, and `normalize(v)` makes one by dividing by the length.
- The **dot product** `a . b = a.x*b.x + a.y*b.y (+ a.z*b.z)` is a single
  number that measures how much two vectors point the same way. For unit
  vectors it is the cosine of the angle between them. We will lean on it
  heavily when we get to lighting.

## Matrices: functions you can multiply

A **matrix** is a grid of numbers. What makes it useful is what it does to a
vector: multiplying a matrix by a vector gives a new vector, where each output
component is the dot product of one *row* of the matrix with the input.

```
  | a  b |   | x |   | a*x + b*y |
  | c  d | * | y | = | c*x + d*y |
```

So a matrix is a function from vectors to vectors, written down as numbers.
Two examples:

```
  scale by (sx, sy)               rotate by angle t (counter-clockwise)

  | sx  0 |   | x |   | sx*x |    | cos t  -sin t |   | x |   | x cos t - y sin t |
  | 0  sy | * | y | = | sy*y |    | sin t   cos t | * | y | = | x sin t + y cos t |
```

The rotation is exactly the arithmetic chapter 04 (Uniforms) wrote out by hand. The
difference is that a matrix is a *value*: you can compute it once on the CPU,
store it, send it, and, most usefully, **multiply two matrices together to get
one matrix that does both jobs**. `R * S` is a single matrix that scales and
then rotates; however many steps you chain, the vertex shader still does one
matrix-times-vector.

## Why 4x4: homogeneous coordinates

There is one motion a 2x2 (or, in 3D, a 3x3) matrix cannot do: **translation**,
moving every point by the same offset. Multiplying the zero vector by any matrix
gives zero, so the origin can never move. Translation is an addition, not a
multiplication.

The trick that fixes this is to add one more component, called **w**, and set
it to 1 for every point. A 3D point becomes `(x, y, z, 1)`, and now a 4x4 matrix
can put the offset in its last column, where it gets multiplied by that 1 and
added in:

```
  | 1  0  0  tx |   | x |   | x + tx |
  | 0  1  0  ty |   | y |   | y + ty |
  | 0  0  1  tz | * | z | = | z + tz |
  | 0  0  0  1  |   | 1 |   |   1    |
```

These four-component coordinates are called **homogeneous coordinates**. The
w component also tells points and directions apart: a *direction* gets `w = 0`,
so the translation column is multiplied by zero and ignored. That is exactly
right: moving an object does not change which way "up" points on it. (In the
next chapter w gets a second job, in perspective projection.)

The three building blocks, written out in 4x4 form:

```
  scale                   rotation about z            translation

  | sx 0  0  0 |          | cos t -sin t  0  0 |       | 1  0  0  tx |
  | 0  sy 0  0 |          | sin t  cos t  0  0 |       | 0  1  0  ty |
  | 0  0  sz 0 |          |  0      0     1  0 |       | 0  0  1  tz |
  | 0  0  0  1 |          |  0      0     0  1 |       | 0  0  0  1  |
```

Even though our quad is flat, we use the full 4x4 form: it is what the GPU's
`setPosition` wants (a `Float4`), and it is what the next chapter needs for 3D.
A 2D quad simply lives at z = 0 and rotates about the z axis, which points out
of the screen.

EACP's `Maths::Mat4` builds all three for you: `Mat4::scale(Vec3)` (or
`Mat4::scale(float)` for the same factor on every axis), `Mat4::rotationZ(radians)`
(plus `rotationX`/`rotationY`), and `Mat4::translation(Vec3)`. A
default-constructed `Mat4` is the **identity**, the matrix with ones on the
diagonal that leaves every vector unchanged.

## Order matters: read right to left

Matrix multiplication is not commutative: `A * B` is generally not `B * A`.
When you write

```
  M = T * R * S          and then         M * v  =  T * (R * (S * v))
```

the matrix nearest the vector acts first. So `T * R * S` **scales first, then
rotates, then translates**, even though you read T first. This is the single
most common source of "my object is in the wrong place" bugs, so it is worth
picturing. Take a quad at the origin, and a rotation R of 90 degrees and a
translation T of 0.5 along x:

```
  T * R  (rotate, then move)              R * T  (move, then rotate)

       y                                       y
       |                                       |  [#]  <- ended up at (0, 0.5):
       |                                       |   ^      the translation was
   ----+----[#]--> x                       ----+---|---> x  rotated too
       |     ^                                 |  (0.5, 0) before rotating
       |  spun in place,
       |  then moved to (0.5, 0)
```

Rotation always turns things about the **origin**. Rotate a quad while it is
still sitting on the origin and it spins on its own centre; rotate it after it
has been moved and it swings around the origin like a stone on a string. That
second behaviour is not always a bug: it is exactly how this chapter's program
makes the quad orbit.

The usual recipe for placing an object is therefore `T * R * S`: size it while
it is centred, turn it while it is centred, and move it last.

## How Mat4 is stored, and how it reaches the GPU

Sixteen floats can be laid out row by row or column by column. `Maths::Mat4`
is **column-major**: `values[column * 4 + row]`, so the translation lives in
`values[12..14]`. That is the layout Metal, HLSL and GLSL all use for a
`float4x4`, so a `Mat4` assigned to a `Uniform<Float4x4>` is copied byte for
byte, with nothing to transpose. (If you ever paste a matrix from a row-major
source and your object flies off, a missing transpose is the first suspect.)

The `operator*` on two `Mat4`s is the ordinary mathematical product, and
`Mat4 * Vec4` multiplies the vector as a column on the right, so everything
above reads the same in code as on paper. In the shader, `Float4x4 * Float4` is
that same product. (`Float4 * Float4x4`, vector on the left, also exists and
means the *transposed* product: stick to matrix-on-the-left.)

## Walking through the code

The vertices are a unit square centred on the origin, in the quad's own
**local** coordinates, with a different colour in each corner so you can see
which way round it is:

```cpp
struct Vertex
{
    Vec2 position;
    Vec3 color;
};

namespace
{
// A unit square centred on the origin: this is the quad in its own "local"
// coordinates, before any transform. Every motion you see comes from the
// matrix, never from editing these numbers.
const Vertex quadVertices[] = {
    {{-0.5f, -0.5f}, {1.0f, 0.3f, 0.3f}}, // bottom left: red
    {{0.5f, -0.5f}, {0.3f, 1.0f, 0.3f}}, // bottom right: green
    {{0.5f, 0.5f}, {0.3f, 0.4f, 1.0f}}, // top right: blue
    {{-0.5f, 0.5f}, {1.0f, 0.9f, 0.3f}}, // top left: yellow
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};
```

Nothing here ever changes. All motion lives in one uniform.

The shader is the coloured quad of chapter 03 (Interpolation) with a single new line: instead of
handing the position straight to `setPosition`, it multiplies it by
`transform`. `float4(position, 0.0f, 1.0f)` is the homogeneous point: z = 0
because the quad is flat, w = 1 because it is a point and must be translated.

```cpp
struct TransformShader final : ShaderProgram
{
    TransformShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);

        // A point gets w = 1 so the matrix's translation column applies to it.
        // One matrix-times-vector replaces all the cos/sin arithmetic a
        // hand-written rotation would need, however many steps the CPU folded
        // into the matrix.
        auto point = float4(position, 0.0f, 1.0f);
        setPosition(transform * point);

        setFragment(float4(varying(color), 1.0f));
    }

    // A Float4x4 uniform is fed from a Maths::Mat4: both are column-major, so
    // the 16 floats cross to the GPU unchanged.
    Uniform<Float4x4> transform;

    EACP_SHADER(transform)
};
} // namespace
```

`Uniform<Float4x4>` is declared exactly like the `Uniform<Float> time` you
already know and listed in `EACP_SHADER` the same way. Because `define()` only
*records* a graph, `transform * point` does no arithmetic on the CPU: it
records a matrix-times-vector node, which the emitter prints as
`transform * point` in Metal (and the equivalent in HLSL and GLSL). It runs once
per vertex on the GPU. Note what is not here: no `cos`, no `sin`, no angles.
The shader does not know or care how the matrix was made.

The view sets up the geometry and the pipeline as before, and advances two
angles every frame, scaled by delta time:

```cpp
struct TransformationsView final : GPUView
{
    TransformationsView()
    {
        shader.setVertices(quadVertices);
        shader.setIndices(quadIndices);
        shader.prepare(sampleCount());
        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        spinAngle += spinSpeed * delta;
        orbitAngle += orbitSpeed * delta;
    }
```

`render` is where this chapter's idea lives. It builds the matrix fresh every
frame from four simple ones:

```cpp
    void render(Frame& frame) override
    {
        // Read every product right to left: that is the order the steps happen
        // to each vertex. Shrink the quad, spin it about its own centre, push it
        // out along x, then rotate that offset about the window's centre, which
        // is what turns a fixed offset into an orbit.
        auto model = Mat4::rotationZ(orbitAngle)
                     * Mat4::translation({orbitRadius, 0.0f, 0.0f})
                     * Mat4::rotationZ(spinAngle) * Mat4::scale(quadSize);

        // Clip space is -1..1 on both axes however wide the window is, so a
        // square drawn there stretches with the window. One last scale on x,
        // applied after everything else, squeezes it back.
        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto aspectCorrection = Mat4::scale({1.0f / aspect, 1.0f, 1.0f});

        shader.transform = aspectCorrection * model;

        auto pass = frame.beginPass({Graphics::Color {0.10f, 0.10f, 0.12f}});
        pass.draw(shader);
    }
```

Read `model` right to left, the order in which each vertex experiences it:

```
  scale(quadSize)            shrink the unit square to 0.35 wide, still centred
  rotationZ(spinAngle)       spin it about its own centre (it is still at the origin)
  translation(radius, 0, 0)  push it out to (0.55, 0)
  rotationZ(orbitAngle)      rotate that about the origin: an orbit
```

The first three are the `T * R * S` recipe. The fourth is the "rotate after
moving" case from the order diagram, used on purpose.

Then one more matrix goes on the very left. Clip space runs from -1 to 1 on both
axes regardless of the window's shape, so in a window twice as wide as it is
tall, one unit of x covers twice as many pixels as one unit of y and our square
would come out stretched. Chapter 05 (Fragment Playground) fixed this in the fragment shader; here it
is just another scale, `1 / aspect` on x, applied *last* so it squeezes the
finished picture rather than the quad before it was rotated. (Put it on the
right instead and the quad would be squashed first and then spun, so it would
look like a parallelogram at most angles.)

Finally, `shader.transform = aspectCorrection * model;` uploads the product.
The CPU did a handful of 4x4 multiplications; the GPU does one per vertex.

## Try it yourself

1. Delete the leftmost `Mat4::rotationZ(orbitAngle)` so the model is plain
   `T * R * S`. The quad now spins in place at (0.55, 0). *Hint: nothing else
   needs to change.*
2. Now see the order bug: swap the translation and the spin, giving
   `Mat4::rotationZ(spinAngle) * Mat4::translation(...) * Mat4::scale(quadSize)`.
   The quad no longer spins on its centre; it swings round the origin, always
   showing the same corner to the middle. *Hint: rotation is always about the
   origin, and after the translation the quad is no longer there.*
3. Make the quad pulse in size by scaling it with `1 + 0.3 * sin(t)` and then
   try a non-uniform scale `Mat4::scale({2.0f, 1.0f, 1.0f})` both rightmost and
   leftmost in `model`. *Hint: on the right it stretches the quad and then
   spins the rectangle; on the left it stretches the already-rotated quad,
   which shears it.*
4. Remove `aspectCorrection` and resize the window. Then put it on the right of
   `model` instead of the left and explain what you see. *Hint: a stretched square, rotated, then un-stretched, is a parallelogram.*
5. Add a second, smaller quad that orbits the first one, like a moon. *Hint:
   reuse the first quad's matrix (without its scale and spin) as a parent, and
   multiply the moon's own `rotationZ * translation * scale` onto its right. You
   will need a second `TransformShader` instance, since each one holds its own
   uniform values.*

## Further reading

- EACP GPU README, [The CPU types a shader value is fed from](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#the-cpu-types-a-shader-value-is-fed-from): `Maths::Mat4` as a column-major `Float4x4`.
- EACP GPU README, [What the EDSL has](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#what-the-edsl-has): the matrix types and the two directions of matrix-vector product.
- `Apps/GPU/Teapot` in EACP builds the same kinds of matrices *inside* the shader (`rotateZ`, `translate`) from scalar uniforms, the alternative to building them on the CPU.
- learnopengl.com, [Transformations](https://learnopengl.com/Getting-started/Transformations), which this chapter follows closely.
- 3Blue1Brown, [Essence of linear algebra](https://www.3blue1brown.com/topics/linear-algebra): the best visual intuition for what a matrix does to space.

Previous: [06. Textures](../06-Textures/README.md) | Next: [08. Coordinate Systems](../08-CoordinateSystems/README.md)
