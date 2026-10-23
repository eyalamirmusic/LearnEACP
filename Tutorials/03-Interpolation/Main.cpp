// Chapter 03: Interpolation.
//
// A quad whose four corners are red, green, blue and yellow. Only the corners
// are given a colour; every pixel in between gets a blend the rasterizer works
// out. The quad is two triangles that share two corners through an index
// buffer.
#include <eacp/GPU/GPU.h>

using namespace eacp;
using namespace GPU;

// Each vertex now carries two attributes. Maths::Vec3 is already known to the
// shader layer as a Float3, the same way Vec2 is a Float2.
struct Vertex
{
    Maths::Vec2 position;
    Maths::Vec3 color;
};

namespace
{
// The four corners of the quad, each stored once.
//
//   3 (yellow) ---- 2 (blue)
//   |                |
//   0 (red) ------- 1 (green)
const Vertex quadVertices[] = {
    {{-0.6f, -0.6f}, {1.0f, 0.0f, 0.0f}}, // 0: bottom left, red
    {{0.6f, -0.6f}, {0.0f, 1.0f, 0.0f}}, // 1: bottom right, green
    {{0.6f, 0.6f}, {0.0f, 0.0f, 1.0f}}, // 2: top right, blue
    {{-0.6f, 0.6f}, {1.0f, 1.0f, 0.0f}}, // 3: top left, yellow
};

// Two triangles, three indices each, naming vertices by their position in
// quadVertices: 0-1-2 is the bottom-right half, 2-3-0 the top-left half. Both
// run counter-clockwise in clip space (y up), which EACP calls front-facing.
const std::uint16_t quadIndices[] = {0, 1, 2, 2, 3, 0};

struct InterpolationShader final : ShaderProgram
{
    InterpolationShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);

        // varying() hands a vertex-stage value on to the fragment stage. The
        // fragment stage never sees one vertex's colour: it sees the three
        // corners' colours blended by how close this pixel is to each.
        auto blended = varying(color);

        setPosition(float4(position, 0.0f, 1.0f));
        setFragment(float4(blended, 1.0f));
    }

    // No uniforms yet: the empty member list EACP_SHADER(...) would write.
    void reflectMembers(ShaderVisitor&) override {}
};
} // namespace

struct InterpolationView final : GPUView
{
    InterpolationView()
    {
        shader.setVertices(quadVertices);

        // With indices set, draw(shader) draws indexed: six indices, so two
        // triangles, built from the four vertices above.
        shader.setIndices(quadIndices);

        shader.prepare(sampleCount());
    }

    void render(Frame& frame) override
    {
        auto pass = frame.beginPass({.clearColor = {0.10f, 0.10f, 0.12f}});
        pass.draw(shader);
    }

    InterpolationShader shader;
};

int main()
{
    return Graphics::runWindowedApp<InterpolationView>();
}
