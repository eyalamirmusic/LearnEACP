// Chapter 04: Uniforms. One value, set from the CPU each frame, read by every
// vertex and every fragment of the draw. Here that value is the time in
// seconds: the vertex stage uses it to spin the quad and the fragment stage
// uses it to make the colours pulse.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace Uniforms
{
namespace
{
// The quad from the previous chapter: four corners, each with its own colour.
struct Vertex
{
    Vec2 position;
    Vec3 color;
};

const Vertex quadVertices[] = {
    {{-0.5f, -0.5f}, {1.0f, 0.3f, 0.3f}}, // bottom left
    {{0.5f, -0.5f}, {0.3f, 1.0f, 0.3f}}, // bottom right
    {{0.5f, 0.5f}, {0.3f, 0.3f, 1.0f}}, // top right
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 0.3f}}, // top left
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

struct SpinningShader final : ShaderProgram
{
    // compile() walks the members EACP_SHADER names, gives each uniform a slot
    // in the uniform block, then calls define() to record the graph.
    SpinningShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);

        // Vertex stage: `time` is the same number for all four corners, so
        // every corner turns by the same angle and the quad spins as one.
        // This is the standard 2D rotation:
        //   x' = x cos(a) - y sin(a)
        //   y' = x sin(a) + y cos(a)
        auto angle = time * 0.8f; // radians: 0.8 per second
        auto c = cos(angle);
        auto s = sin(angle);
        auto rotated = float2(position.x() * c - position.y() * s,
                              position.x() * s + position.y() * c);

        setPosition(float4(rotated, 0.0f, 1.0f));

        // Fragment stage: the interpolated colour is scaled by a brightness
        // that swings between 0.4 and 1.0 a little under once a second.
        // Reading `time` here as well puts the uniform block in both stages.
        auto pulse = 0.7f + 0.3f * sin(time * 5.0f);
        setFragment(float4(varying(color) * pulse, 1.0f));
    }

    // A uniform: one value for the whole draw, written from C++ before
    // pass.draw(). Not part of Vertex, so it has no per-vertex storage.
    Uniform<Float> time;

    // Lists the uniform members so compile() can find them by name.
    EACP_SHADER(time)
};
} // namespace

struct UniformsView final : GPUView
{
    UniformsView()
    {
        shader.setVertices(quadVertices);
        shader.setIndices(quadIndices);
        shader.prepare(sampleCount());

        // Render every display refresh instead of only when something asks
        // for a repaint, and call update() before each frame.
        setContinuous(true);
    }

    // Advance the clock by how long the last frame actually took, so the
    // animation runs at the same speed at 60 Hz, 120 Hz or after a hitch.
    void update(Threads::FrameTime frameTime) override
    {
        seconds += static_cast<float>(frameTime.delta);
    }

    void render(Frame& frame) override
    {
        // Copy the CPU value into the uniform; draw() uploads it.
        shader.time = seconds;

        auto pass = frame.beginPass({Graphics::Color {0.10f, 0.10f, 0.12f}});
        pass.draw(shader);
    }

    SpinningShader shader;
    float seconds = 0.0f;
};
} // namespace Uniforms

LEARNEACP_CHAPTER(Uniforms::UniformsView)
