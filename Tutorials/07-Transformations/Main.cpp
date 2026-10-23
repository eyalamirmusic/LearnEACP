// 07. Transformations: a quad that spins on its own centre while it orbits
// the middle of the window, positioned by one 4x4 matrix built on the CPU.
#include <eacp/GPU/GPU.h>

using namespace eacp;
using namespace GPU;
using namespace Maths;

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

    static constexpr float quadSize = 0.35f;
    static constexpr float orbitRadius = 0.55f;
    static constexpr float spinSpeed = 2.0f; // radians per second
    static constexpr float orbitSpeed = 0.7f;

    TransformShader shader;
    float spinAngle = 0.0f;
    float orbitAngle = 0.0f;
};

int main()
{
    return Graphics::runWindowedApp<TransformationsView>();
}
