// Chapter 05: Fragment Playground. Two triangles cover the whole window, and
// the fragment shader decides the colour of every pixel from its position
// alone: a sky gradient, a pulsing sun cut off by a hard horizon, a soft halo
// ring and a small moon orbiting it.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace FragmentPlayground
{
namespace
{
struct Vertex
{
    Vec2 position; // clip space, -1..1
    Vec2 uv; // 0..1 across the window, (0, 0) at the bottom left
};

// A quad that exactly covers clip space, so it covers the whole window.
const Vertex screenVertices[] = {
    {{-1.0f, -1.0f}, {0.0f, 0.0f}}, // bottom left
    {{1.0f, -1.0f}, {1.0f, 0.0f}}, // bottom right
    {{1.0f, 1.0f}, {1.0f, 1.0f}}, // top right
    {{-1.0f, 1.0f}, {0.0f, 1.0f}}, // top left
};

const std::uint16_t screenIndices[] = {0, 1, 2, 0, 2, 3};

struct PlaygroundShader final : ShaderProgram
{
    PlaygroundShader() { compile(); }

    void define() override
    {
        // The vertex stage has nothing to do but pass the corners through.
        auto position = vertexInput(&Vertex::position);
        setPosition(float4(position, 0.0f, 1.0f));

        // Everything below runs once per pixel.
        auto uv = varying(vertexInput(&Vertex::uv));

        // An all-literal colour needs constant() to put it on the graph.
        auto rgb = [this](float r, float g, float b)
        { return float3(constant(r), g, b); };

        // 1. A gradient: uv.y is 0 at the bottom and 1 at the top, so mix()
        //    walks from the first colour to the second up the window.
        auto color = mix(rgb(0.95f, 0.45f, 0.35f), rgb(0.10f, 0.08f, 0.30f), uv.y());

        // 2. Aspect-corrected coordinates. p runs -1..1 vertically, and
        //    horizontally by the same scale, so a circle stays round in a
        //    window of any shape.
        auto aspect = resolution.x() / resolution.y();
        auto centered = uv * 2.0f - 1.0f;
        auto p = float2(centered.x() * aspect, centered.y());

        // One pixel measured in p units: p spans 2 units over the height.
        auto pixel = 2.0f / resolution.y();

        // 3. A signed-distance circle: negative inside, zero on the edge,
        //    positive outside. Its radius breathes with time.
        auto sunCenter = float2(constant(0.0f), -0.25f);
        auto toSun = p - sunCenter;
        auto radius = 0.45f + 0.03f * sin(time * 2.0f);
        auto sun = length(toSun) - radius;

        // smoothstep turns the distance into coverage with an edge one pixel
        // wide, which is antialiasing for free.
        auto sunMask = 1.0f - smoothstep(0.0f, pixel, sun);
        auto sunColor =
            mix(rgb(1.0f, 0.35f, 0.25f), rgb(1.0f, 0.85f, 0.35f), uv.y());
        color = mix(color, sunColor, sunMask);

        // 4. A ring is the circle's distance folded with abs(): zero along a
        //    circle of the halo's radius, minus half its thickness.
        auto halo = abs(length(toSun) - (radius + 0.12f)) - 0.012f;

        // 5. A second circle: a moon that circles the sun once every 2 pi
        //    (about six) seconds.
        auto orbit = float2(cos(time), sin(time)) * 0.75f;
        auto moon = length(toSun - orbit) - 0.07f;

        // min() of two distances is the distance to whichever is nearer: the
        // union of the two shapes, drawn with one mask.
        auto outlines = min(halo, moon);
        auto outlineMask = 1.0f - smoothstep(0.0f, pixel, outlines);
        color = mix(color, rgb(1.0f, 0.95f, 0.85f), outlineMask);

        // 6. A hard horizon. step(edge, x) is 0 below the edge and 1 above,
        //    with nothing in between, so this edge is visibly jagged.
        auto horizon = 1.0f - step(0.375f, uv.y());
        color = mix(color, rgb(0.06f, 0.05f, 0.12f), horizon);

        setFragment(float4(color, 1.0f));
    }

    // The drawable's size in pixels, and the time in seconds.
    Uniform<Float2> resolution;
    Uniform<Float> time;

    EACP_SHADER(resolution, time)
};
} // namespace

struct FragmentPlaygroundView final : GPUView
{
    FragmentPlaygroundView()
    {
        shader.setVertices(screenVertices);
        shader.setIndices(screenIndices);
        shader.prepare(sampleCount());
        setContinuous(true);
    }

    void update(Threads::FrameTime frameTime) override
    {
        seconds += static_cast<float>(frameTime.delta);
    }

    void render(Frame& frame) override
    {
        // Read the size off the frame every time: it is the size of what is
        // being drawn into right now, in device pixels, so it is right during
        // a live resize and on a high-density display.
        auto size = frame.pixelSize();
        shader.resolution = Vec2 {size.x, size.y};
        shader.time = seconds;

        auto pass = frame.beginPass({});
        pass.draw(shader);
    }

    PlaygroundShader shader;
    float seconds = 0.0f;
};
} // namespace FragmentPlayground

LEARNEACP_CHAPTER(FragmentPlayground::FragmentPlaygroundView)
