// 08. Coordinate Systems: a spinning cube seen through a perspective camera,
// with a depth buffer so near faces hide far ones and back-face culling so the
// hidden half is never drawn at all.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace CoordinateSystems
{
// Flip these to see what each one is for. With both off, the cube's faces draw
// in index order, so a far face can paint over a near one. Either one alone
// fixes a single convex cube; a real scene needs the depth buffer.
constexpr bool useDepthBuffer = true;
constexpr bool cullBackFaces = true;

struct Vertex
{
    Vec3 position;
    Vec3 color;
};

namespace
{
// A cube of side 1 centred on the origin, in its own local space. Each face has
// its own four vertices (24 in all) rather than sharing the 8 corners, because
// a vertex carries one colour and each corner belongs to three faces of
// different colours.
//
// Every face lists its corners counter-clockwise as seen from outside the cube,
// looking at that face. After the camera and projection, the faces turned
// towards the viewer are still counter-clockwise, which is what EACP calls
// front-facing.
const Vertex cubeVertices[] = {
    // +X (right): red
    {{0.5f, -0.5f, 0.5f}, {0.90f, 0.30f, 0.30f}},
    {{0.5f, -0.5f, -0.5f}, {0.90f, 0.30f, 0.30f}},
    {{0.5f, 0.5f, -0.5f}, {0.90f, 0.30f, 0.30f}},
    {{0.5f, 0.5f, 0.5f}, {0.90f, 0.30f, 0.30f}},
    // -X (left): cyan
    {{-0.5f, -0.5f, -0.5f}, {0.30f, 0.85f, 0.90f}},
    {{-0.5f, -0.5f, 0.5f}, {0.30f, 0.85f, 0.90f}},
    {{-0.5f, 0.5f, 0.5f}, {0.30f, 0.85f, 0.90f}},
    {{-0.5f, 0.5f, -0.5f}, {0.30f, 0.85f, 0.90f}},
    // +Y (top): green
    {{-0.5f, 0.5f, 0.5f}, {0.35f, 0.85f, 0.35f}},
    {{0.5f, 0.5f, 0.5f}, {0.35f, 0.85f, 0.35f}},
    {{0.5f, 0.5f, -0.5f}, {0.35f, 0.85f, 0.35f}},
    {{-0.5f, 0.5f, -0.5f}, {0.35f, 0.85f, 0.35f}},
    // -Y (bottom): magenta
    {{-0.5f, -0.5f, -0.5f}, {0.85f, 0.35f, 0.85f}},
    {{0.5f, -0.5f, -0.5f}, {0.85f, 0.35f, 0.85f}},
    {{0.5f, -0.5f, 0.5f}, {0.85f, 0.35f, 0.85f}},
    {{-0.5f, -0.5f, 0.5f}, {0.85f, 0.35f, 0.85f}},
    // +Z (front): blue
    {{-0.5f, -0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    {{0.5f, -0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    {{0.5f, 0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    {{-0.5f, 0.5f, 0.5f}, {0.30f, 0.45f, 0.95f}},
    // -Z (back): yellow
    {{0.5f, -0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
    {{-0.5f, -0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
    {{-0.5f, 0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
    {{0.5f, 0.5f, -0.5f}, {0.95f, 0.85f, 0.30f}},
};

// Two counter-clockwise triangles per face: corners 0-1-2 and 0-2-3.
const std::uint16_t cubeIndices[] = {
    0,  1,  2,  0,  2,  3, // +X
    4,  5,  6,  4,  6,  7, // -X
    8,  9,  10, 8,  10, 11, // +Y
    12, 13, 14, 12, 14, 15, // -Y
    16, 17, 18, 16, 18, 19, // +Z
    20, 21, 22, 20, 22, 23, // -Z
};

struct CubeShader final : ShaderProgram
{
    CubeShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);

        // Local space straight to clip space in one multiply: the CPU has
        // already folded model, view and projection into a single matrix.
        setPosition(modelViewProjection * float4(position, 1.0f));

        setFragment(float4(varying(color), 1.0f));
    }

    Uniform<Float4x4> modelViewProjection;

    EACP_SHADER(modelViewProjection)
};
} // namespace

struct CoordinateSystemsView final : GPUView
{
    CoordinateSystemsView()
    {
        // The depth buffer is two switches that must agree: the view allocates
        // the buffer (and clears it every frame), the pipeline tests against it.
        setDepth(useDepthBuffer);

        shader.setVertices(cubeVertices);
        shader.setIndices(cubeIndices);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = useDepthBuffer;
        pipeline.cullMode = cullBackFaces ? CullMode::Back : CullMode::None;
        shader.prepare(pipeline);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        angle += radiansPerSecond * static_cast<float>(time.delta);
    }

    void render(Frame& frame) override
    {
        // Local -> world: tumble the cube about two axes so every face shows.
        auto model = Mat4::rotationY(angle) * Mat4::rotationX(angle * 0.6f);

        // World -> view: a camera up and in front of the origin, looking at it.
        auto view =
            Mat4::lookAt({0.0f, 1.0f, 2.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});

        // View -> clip: a 45 degree vertical field of view, shaped to the window
        // so the cube stays square, seeing from 0.1 to 100 units away.
        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);

        // Right to left, as always: model first, projection last.
        shader.modelViewProjection = projection * view * model;

        auto pass = frame.beginPass({Graphics::Color {0.10f, 0.10f, 0.12f}});
        pass.draw(shader);
    }

    static constexpr float radiansPerSecond = 0.8f;

    CubeShader shader;
    float angle = 0.0f;
};
} // namespace CoordinateSystems

LEARNEACP_CHAPTER(CoordinateSystems::CoordinateSystemsView)
