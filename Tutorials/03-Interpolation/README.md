# 03. Interpolation

Last chapter's triangle was one flat colour. This chapter gives every vertex a
colour of its own and lets the GPU fill in the rest, producing a smooth blend
across the shape. Then it grows the triangle into a quad (a rectangle made of
two triangles) without repeating any vertex data, using an **index buffer**. At
the end you will understand how data travels from the vertex stage to the
fragment stage, what the rasterizer does with it on the way (**interpolation**),
why index buffers exist, and what **winding order** means for a triangle.

## Passing data between stages: varyings

So far a vertex has had one attribute, its position. A vertex can carry more:
a colour, a texture coordinate, a normal. Each is another field in the vertex
struct and another `vertexInput` in the shader.

But the colour is wanted by the fragment stage, and the fragment stage does not
run per vertex. It runs per fragment, and a fragment usually lies somewhere
*between* the vertices. So which vertex's colour should it get?

The answer is: a blend of all three. A value that the vertex stage hands on to
the fragment stage is called a **varying**, because it varies across the
triangle. The vertex stage writes one value per vertex; the rasterizer, while
it works out which pixels the triangle covers, also computes a blended value of
every varying for each fragment; the fragment stage reads the blended value.

```
  VERTEX STAGE                 RASTERIZER                    FRAGMENT STAGE
  (per vertex)                 (per fragment)                (per fragment)

  v0: colour = red    --\
  v1: colour = green  ---+-->  blend the three colours  -->  colour arrives
  v2: colour = blue   --/      by where this fragment        already blended
                               sits in the triangle
```

## How the rasterizer blends: barycentric interpolation

Any point inside a triangle can be described by three **weights**, one per
corner, that are each between 0 and 1 and add up to 1. A weight says how close
the point is to that corner: at a corner its own weight is 1 and the others are
0; at the exact centre all three are 1/3; on an edge, the weight of the opposite
corner is 0. These are called **barycentric coordinates**.

```
                     v0 (red)
                        /\
                       /  \           at v0:        (1,   0,   0)
                      /    \          at the centre: (1/3, 1/3, 1/3)
                     /  *   \         at * :         (0.6, 0.2, 0.2)
                    /   c    \        midpoint of v1-v2: (0, 0.5, 0.5)
                   /          \
                  /____________\
           v1 (green)    m     v2 (blue)
```

For each fragment the rasterizer finds its weights `(w0, w1, w2)` and computes

```
    colour = w0 * colour(v0) + w1 * colour(v1) + w2 * colour(v2)
```

So the point `*` above, mostly near the red corner, gets `0.6 * red + 0.2 *
green + 0.2 * blue`, a reddish colour, and the midpoint `m` of the bottom edge
gets half green and half blue, a teal. Do that for every fragment and you get a
smooth gradient. The same mechanism carries texture coordinates and normals in
later chapters: anything you put through a varying is blended this way.

(One detail for later: in 3D, the GPU adjusts these weights for perspective so
that a gradient on a surface tilted away from you does not look distorted. With
`w = 1`, as in this chapter, the adjustment does nothing and the blend is
exactly the linear one above.)

## From one triangle to a quad: index buffers

GPUs draw triangles. To draw a rectangle you draw two triangles that share a
diagonal. Written out as plain triangles, that is six vertices:

```
   3 ---------- 2          triangle A: 0, 1, 2
   |          / |          triangle B: 2, 3, 0
   |   B    /   |
   |      /     |          vertices 0 and 2 belong to both
   |    /   A   |
   |  /         |
   0 ---------- 1
```

Vertices 0 and 2 appear twice. For a quad that is a small waste, but in a real
mesh almost every vertex is shared by about six triangles. Storing each copy
separately means storing and transforming each vertex six times.

An **index buffer** fixes that. You store each unique vertex once, in the
vertex buffer, and then describe the triangles with a second, much smaller list
of integers: the indices of the vertices that make up each triangle. Our quad
becomes four vertices plus six indices `0, 1, 2, 2, 3, 0`. Drawing with an index
buffer is called **indexed drawing**. As a bonus, the GPU caches the vertex
stage's results, so a vertex used by several nearby triangles is often shaded
only once.

Indices are usually 16-bit (up to 65,536 vertices per mesh) or 32-bit integers.
EACP takes either, as `std::uint16_t` or `std::uint32_t`, and picks the index
format from the type you pass.

## Winding order

Look at the order of the indices again. Triangle A goes `0 -> 1 -> 2`: bottom
left, bottom right, top right. Traced on screen, that path turns
**counter-clockwise**. Triangle B, `2 -> 3 -> 0`, is counter-clockwise too.

The direction in which a triangle's vertices go round is its **winding**, and it
is how the GPU tells the **front** of a triangle from its **back**. In EACP, a
triangle whose vertices run counter-clockwise in clip space (with y up, the
space `setPosition` writes) is **front-facing**; a clockwise one is
back-facing.

Why would that matter? For a closed 3D object, such as a cube, the triangles
facing away from the camera are always hidden behind the ones facing it. The
GPU can skip them entirely, which is called **back-face culling** and roughly
halves the work. But culling only works if every triangle in the mesh is wound
consistently, so it is a good habit to get right from the start.

In this chapter nothing is culled. EACP's default is `CullMode::None`: both
faces are drawn, so a clockwise triangle would appear just the same. Culling is
turned on through `prepare`'s descriptor form, and chapter 08 (Coordinate
Systems) uses it on a spinning cube.

## Walking through the code

### A vertex with a colour

```cpp
struct Vertex
{
    Maths::Vec2 position;
    Maths::Vec3 color;
};
```

The vertex gained a second field. `Maths::Vec3` is three floats, which the
shader layer knows as a `Float3`, just as `Vec2` is a `Float2`. The colour is
red, green, blue; there is no alpha per vertex, as we will add a fixed alpha of 1
in the shader.

### The four corners

```cpp
const Vertex quadVertices[] = {
    {{-0.6f, -0.6f}, {1.0f, 0.0f, 0.0f}}, // 0: bottom left, red
    {{0.6f, -0.6f}, {0.0f, 1.0f, 0.0f}}, // 1: bottom right, green
    {{0.6f, 0.6f}, {0.0f, 0.0f, 1.0f}}, // 2: top right, blue
    {{-0.6f, 0.6f}, {1.0f, 1.0f, 0.0f}}, // 3: top left, yellow
};
```

Four unique vertices, each with a clip-space position and a colour. The order
of this array matters only because the index buffer refers to vertices by their
position in it.

### The six indices

```cpp
const std::uint16_t quadIndices[] = {0, 1, 2, 2, 3, 0};
```

Every three indices form one triangle: `0, 1, 2` and then `2, 3, 0`, both
counter-clockwise as described above. The element type matters: `setIndices`
has overloads for arrays of `std::uint16_t` and `std::uint32_t` (plus
pointer-and-count versions of each), and the type you pick becomes the index
format. A plain `int` array will not compile.

### The shader

```cpp
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);
```

Two vertex inputs now. Each `vertexInput` records the field's type and byte
offset, so the vertex layout (a `Float2` at offset 0 and a `Float3` at offset 8,
20 bytes per vertex) comes straight from the struct. `color` is a `Float3`
handle that lives in the vertex stage: it is one vertex's colour.

```cpp
        // varying() hands a vertex-stage value on to the fragment stage. The
        // fragment stage never sees one vertex's colour: it sees the three
        // corners' colours blended by how close this pixel is to each.
        auto blended = varying(color);
```

`varying` is the bridge between the stages. It takes a vertex-stage handle and
returns a handle of the same type that lives in the fragment stage. In the
generated source it becomes an extra output of the vertex function and an extra
input of the fragment function, and the rasterizer interpolates it in between.

```cpp
        setPosition(float4(position, 0.0f, 1.0f));
        setFragment(float4(blended, 1.0f));
```

The position is the same as in the last chapter. The fragment colour is the
interpolated colour with an alpha of 1. Because `blended` is a handle,
`float4(blended, 1.0f)` needs no `constant()` this time.

What if you skipped `varying` and wrote `setFragment(float4(color, 1.0f))`,
reading the vertex input directly? Try it: the program still works and looks
the same. A fragment stage has no way to read a vertex attribute, so when
EACP's emitter sees one read there it quietly promotes it to a varying for you.
Write the `varying` anyway. It marks, in your code, the exact point where a
value leaves the vertex stage and starts being interpolated, which matters as
soon as the vertex stage computes something before handing it on.

### Drawing indexed

```cpp
        shader.setVertices(quadVertices);

        // With indices set, draw(shader) draws indexed: six indices, so two
        // triangles, built from the four vertices above.
        shader.setIndices(quadIndices);
```

`setIndices` uploads the indices into an index buffer that the shader owns, just
as `setVertices` does for the vertices. Nothing changes in `render`:

```cpp
    void render(Frame& frame) override
    {
        auto pass = frame.beginPass({.clearColor = {0.10f, 0.10f, 0.12f}});
        pass.draw(shader);
    }
```

`pass.draw(shader)` checks whether the shader has indices. If it does, it issues
an indexed draw of `indexCount()` indices (six), in the format `setIndices`
chose; otherwise it draws `vertexCount()` vertices, as in the last chapter. So
setting indices is all it takes to switch to indexed drawing.

## What you should see

A rectangle with a red bottom-left corner, green bottom-right, blue top-right
and yellow top-left, blending smoothly in between. Look closely along the
diagonal from bottom left to top right: there is a faint purple ridge. That is
the seam between the two triangles. Each triangle only blends its own three
corners, so every point on the shared diagonal is a mix of just red and blue,
and the green and yellow corners have no say there. The colour at the centre of
the quad is purple, not the grey-ish average of all four corners. Interpolation
is always per triangle.

## Try it yourself

1. Change the corner colours, for example to make a vertical gradient from
   black at the bottom to white at the top. *Hint: give both bottom vertices
   the same colour, and both top vertices another.*
2. Flip the diagonal. Make the triangles `0, 1, 3` and `1, 2, 3`, and see the
   seam move. *Hint: keep both triangles counter-clockwise.*
3. Prove culling is off. Reverse one triangle to `2, 1, 0` and check it still
   draws. Then prepare with
   `shader.prepare({.sampleCount = sampleCount(), .cullMode = CullMode::Back});`
   and watch it disappear. *Hint: under `Back`, a clockwise triangle is a back
   face.*
4. Draw a hexagon with a white centre and coloured rim. *Hint: seven vertices
   (a centre plus six around it, from `std::cos` and `std::sin`) and six
   triangles that all share the centre, so eighteen indices.*
5. Remove the seam by using five vertices: add a centre vertex whose colour is
   the average of the four corners, and draw four triangles that fan around it.
   *Hint: this is the hexagon exercise with four sides.*

## Further reading

- EACP GPU README, [Face culling, and which way round front is](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#face-culling-and-which-way-round-front-is):
  the winding convention, and why it is stated in clip space.
- EACP GPU README, [Pipeline state](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#pipeline-state):
  the descriptor form of `prepare`, with `cullMode` and `frontFace`.
- `Apps/GPU/RotatingTriangle` in EACP: per-vertex colours through `varying`, the
  same technique as this chapter.
- learnopengl.com, [Shaders](https://learnopengl.com/Getting-started/Shaders)
  (the "More attributes" section shows the same colourful triangle) and
  [Hello Triangle](https://learnopengl.com/Getting-started/Hello-Triangle)
  (element buffer objects, OpenGL's name for index buffers).
- learnopengl.com, [Face culling](https://learnopengl.com/Advanced-OpenGL/Face-culling):
  winding order and culling in more depth.
- Scratchapixel, [Rasterization: a practical implementation](https://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/overview-rasterization-algorithm.html):
  how barycentric coordinates are computed, if you want to see the maths.

Previous: [02. Hello Triangle](../02-HelloTriangle/README.md) | Next: [04. Uniforms](../04-Uniforms/README.md)
