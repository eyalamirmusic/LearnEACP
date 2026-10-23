// Chapter 02: Hello Triangle.
//
// One triangle in one colour: the first shader. Three corners go in, the
// vertex stage places them, the rasterizer finds the pixels they cover and the
// fragment stage colours each one.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

using namespace eacp;
using namespace GPU;

namespace HelloTriangle
{
// The CPU-side description of one vertex. This struct *is* the vertex layout:
// vertexInput() below reads the field's type and offset straight out of it, so
// there is no separate attribute table to keep in sync.
struct Vertex
{
    Maths::Vec2 position;
};

namespace
{
// Three corners in clip space, where the visible window spans -1 to 1 on both
// axes, x to the right and y up, whatever the window's size in pixels.
const Vertex triangleVertices[] = {
    {{0.0f, 0.5f}}, // top
    {{-0.5f, -0.5f}}, // bottom left
    {{0.5f, -0.5f}}, // bottom right
};

// A shader written as a C++ struct. define() is not run on the GPU: it runs
// once, on the CPU, inside compile(), and every call in it records a node in a
// graph. The emitters then print that graph as Metal, HLSL or GLSL source.
struct TriangleShader final : ShaderProgram
{
    TriangleShader() { compile(); }

    void define() override
    {
        // Vertex stage: runs once per vertex. `position` is a handle to the
        // Float2 the GPU will read from Vertex::position, not a number.
        auto position = vertexInput(&Vertex::position);

        // The vertex stage's one required output: where this vertex lands in
        // clip space, as (x, y, z, w). z = 0 and w = 1 leave x and y as given.
        setPosition(float4(position, 0.0f, 1.0f));

        // Fragment stage: runs once per pixel the triangle covers and returns
        // its colour as (red, green, blue, alpha). Every value here is a
        // literal, so constant() supplies the first one as a shader value.
        setFragment(float4(constant(1.0f), 0.5f, 0.2f, 1.0f));
    }

    // A shader normally lists its uniforms (values set from C++ per draw) with
    // EACP_SHADER(name, ...), which writes this function for you. With no
    // uniforms yet, we write the empty version by hand: EACP_SHADER() would
    // leave its parameter unused and draw a compiler warning.
    void reflectMembers(ShaderVisitor&) override {}
};
} // namespace

struct HelloTriangleView final : GPUView
{
    HelloTriangleView()
    {
        // Copy the vertices into a GPU buffer the shader owns.
        shader.setVertices(triangleVertices);

        // Build the GPU pipeline: compile the generated source and fix the
        // state it draws with. The sample count must match the view's.
        shader.prepare(sampleCount());
    }

    void render(Frame& frame) override
    {
        auto pass = frame.beginPass({.clearColor = {0.10f, 0.10f, 0.12f}});

        // Bind the pipeline and vertex buffer, then draw all three vertices.
        pass.draw(shader);
    }

    TriangleShader shader;
};
} // namespace HelloTriangle

LEARNEACP_CHAPTER(HelloTriangle::HelloTriangleView)
