// 19. Anti-Aliasing: the same scene of thin, high-contrast geometry is rendered
// twice, into a single-sampled texture and into a multisampled one, and the two
// are shown side by side: aliased on the left, multisampled on the right, each
// with a magnified inset of the same spot. A mouse click steps the right-hand
// side through the sample counts the GPU supports.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace AntiAliasing
{
struct Vertex
{
    Vec3 position;
    Vec3 color;
};

// The full-screen quad of chapter 16.
struct QuadVertex
{
    Vec2 position;
    Vec2 uv;
};

namespace
{
struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

// A flat fan of long, thin spokes lying in the xz plane, the worst case for
// aliasing: every spoke is a sliver a pixel or two wide at its outer end and
// narrower than a pixel towards the hub, at every angle there is, white on a
// near-black background.
Mesh makeFan()
{
    constexpr auto spokeCount = 72;
    constexpr auto innerRadius = 0.35f;
    constexpr auto outerRadius = 2.4f;
    constexpr auto halfWidth = 0.004f; // half the spoke's angle, in radians

    auto mesh = Mesh {};

    for (auto i = 0; i < spokeCount; ++i)
    {
        auto angle = 2.0f * pi * (float) i / (float) spokeCount;

        // White and pale blue alternate, so neighbouring spokes are told apart.
        auto color = i % 2 == 0 ? Vec3 {1.0f, 1.0f, 1.0f} : Vec3 {0.6f, 0.85f, 1.0f};

        auto point = [&](float radius, float a)
        { return Vec3 {radius * std::cos(a), 0.0f, radius * std::sin(a)}; };

        auto first = (std::uint16_t) mesh.vertices.size();

        // The tip at the hub, then the two corners of the wide end. Culling is
        // off for the fan, so the winding does not matter.
        mesh.vertices.push_back({point(innerRadius, angle), color});
        mesh.vertices.push_back({point(outerRadius, angle - halfWidth), color});
        mesh.vertices.push_back({point(outerRadius, angle + halfWidth), color});

        mesh.indices.push_back(first);
        mesh.indices.push_back(first + 1);
        mesh.indices.push_back(first + 2);
    }

    return mesh;
}

// Chapter 08's cube, one flat colour per face, counter-clockwise from outside.
// Its silhouette and the creases between its faces are long straight edges
// that turn slowly through every slope: the classic staircase.
Mesh makeCube()
{
    struct Face
    {
        Vec3 normal, across1, across2, color;
    };

    const Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, {0.95f, 0.55f, 0.20f}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {0.95f, 0.55f, 0.20f}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, {1.00f, 0.90f, 0.70f}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, {0.30f, 0.12f, 0.05f}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0.55f, 0.25f, 0.08f}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0.55f, 0.25f, 0.08f}},
    };

    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    auto mesh = Mesh {};

    for (const auto& face: faces)
    {
        auto first = (std::uint16_t) mesh.vertices.size();

        for (const auto& corner: corners)
        {
            auto position =
                (face.normal + face.across1 * corner[0] + face.across2 * corner[1])
                * 0.5f;
            mesh.vertices.push_back({position, face.color});
        }

        for (auto i: {0, 1, 2, 0, 2, 3})
            mesh.indices.push_back((std::uint16_t) (first + i));
    }

    return mesh;
}

const Mesh fanMesh = makeFan();
const Mesh cubeMesh = makeCube();

// Unlit and flat-coloured on purpose: lighting would soften the edges we want
// to look at.
struct SceneShader final : ShaderProgram
{
    SceneShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto color = vertexInput(&Vertex::color);

        setPosition(modelViewProjection * float4(position, 1.0f));
        setFragment(float4(varying(color), 1.0f));
    }

    Uniform<Float4x4> modelViewProjection;

    EACP_SHADER(modelViewProjection)
};

// Everything that draws the scene at one sample count. A pipeline bakes in the
// sample count of the target it draws into, so each count needs its own.
struct Scene
{
    explicit Scene(int samples)
        : sampleCount(samples)
    {
        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount;
        pipeline.depth = true;
        pipeline.colorFormat = pixelFormatFor(targetFormat);

        fan.setVertices(fanMesh.vertices.data(), (int) fanMesh.vertices.size());
        fan.setIndices(fanMesh.indices.data(), (int) fanMesh.indices.size());
        fan.prepare(pipeline);

        pipeline.cullMode = CullMode::Back;
        cube.setVertices(cubeMesh.vertices.data(), (int) cubeMesh.vertices.size());
        cube.setIndices(cubeMesh.indices.data(), (int) cubeMesh.indices.size());
        cube.prepare(pipeline);
    }

    // The colour texture the scene renders into, at this scene's sample count.
    // Above 1 the texture grows a multisampled twin that the pass really draws
    // into, and resolves into this one when the pass ends.
    void resize(int width, int height)
    {
        auto descriptor = TextureDescriptor {};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.format = targetFormat;
        descriptor.renderTarget = true;
        descriptor.depth = true; // multisampled too, at the same count
        descriptor.sampleCount = sampleCount;

        target.emplace(Device::shared().makeTexture(descriptor));
    }

    void draw(Frame& frame, float angle)
    {
        auto aspect = (float) target->width() / (float) target->height();
        auto view = Mat4::lookAt({0.0f, 2.2f, 3.6f}, {0.0f, 0.2f, 0.0f}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        auto pass =
            frame.beginPass(*target, {Graphics::Color {0.04f, 0.04f, 0.06f}});

        fan.modelViewProjection = viewProjection * Mat4::rotationY(angle * 0.25f);
        pass.draw(fan);

        cube.modelViewProjection =
            viewProjection * Mat4::translation({0.0f, 0.7f, 0.0f})
            * Mat4::rotationY(angle) * Mat4::rotationX(angle * 0.7f)
            * Mat4::scale(0.8f);
        pass.draw(cube);
    }

    static constexpr auto targetFormat = TextureFormat::RGBA8Unorm;

    int sampleCount;
    SceneShader fan;
    SceneShader cube;
    std::optional<Texture> target;
};

// Clip-space y = +1 reads v = 0, as in chapter 16.
const QuadVertex screenQuad[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{1.0f, -1.0f}, {1.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-1.0f, 1.0f}, {0.0f, 0.0f}},
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

// 1 inside the square of half-size `half` around `center`, 0 outside.
Float insideSquare(const Float2& p, const Float2& center, const Float& half)
{
    auto d = abs(p - center);
    return step(d.x(), half) * step(d.y(), half);
}

// Shows one resolved scene texture, one texel per pixel, with a magnifier: a
// square inset in the bottom-right corner shows the small square around
// `focus`, blown up `zoom` times with nearest filtering so that every pixel of
// the original becomes a visible block. A thin frame marks both squares.
struct CompositeShader final : ShaderProgram
{
    CompositeShader()
    {
        scene.sampling = {TextureFilter::Nearest, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));

        setPosition(float4(position, 0.0f, 1.0f));

        // Work in pixels of the texture from here on.
        auto pixel = uv * size;
        auto sourceHalf = insetHalf / zoom;

        // Inside the inset, read from around the focus instead, zoom times
        // closer. A plain lerp between the two coordinates by a 0/1 mask is
        // this graph's if/else.
        auto inInset = insideSquare(pixel, insetCenter, insetHalf);
        auto zoomed = focus + (pixel - insetCenter) / zoom;
        auto source = pixel + (zoomed - pixel) * inInset;

        auto color = sample(scene, source / size).xyz();

        // A frame round the inset and round the square it magnifies.
        auto border = frameWidth;
        auto insetFrame =
            insideSquare(pixel, insetCenter, insetHalf + border) - inInset;
        auto focusFrame = (insideSquare(pixel, focus, sourceHalf + border)
                           - insideSquare(pixel, focus, sourceHalf))
                          * (1.0f - inInset);
        auto frame = max(insetFrame, focusFrame);

        // One dot per sample in the top-left corner, so each pane says what it
        // is. The loop is C++: it leaves eight dot tests in the shader, and
        // step() switches off the ones past the sample count.
        auto dots = constant(0.0f);

        for (auto i = 0; i < 8; ++i)
        {
            auto center =
                float2(dotSize * (2.5f * (float) i + 2.0f), dotSize * 2.0f);
            auto lit = step((float) i + 0.5f, samples);
            dots = max(dots, insideSquare(pixel, center, dotSize * 0.5f) * lit);
        }

        frame = max(frame, dots);

        auto frameColor = float3(constant(1.0f), 0.8f, 0.2f);
        setFragment(float4(color + (frameColor - color) * frame, 1.0f));
    }

    Uniform<Texture2D> scene;
    Uniform<Float2> size; // of the texture, in pixels
    Uniform<Float2> focus; // centre of the magnified square, in pixels
    Uniform<Float2> insetCenter; // centre of the inset, in pixels
    Uniform<Float> insetHalf; // half the inset's side, in pixels
    Uniform<Float> zoom;
    Uniform<Float> frameWidth; // in pixels
    Uniform<Float> samples; // how many dots to show
    Uniform<Float> dotSize; // in pixels

    EACP_SHADER(scene,
                size,
                focus,
                insetCenter,
                insetHalf,
                zoom,
                frameWidth,
                samples,
                dotSize)
};

// The sample counts the right-hand side steps through on a click, keeping
// only those this GPU can render at. 4 is never refused: Metal, D3D12 and
// Vulkan all promise it for an ordinary colour format and depth.
std::vector<int> supportedCounts()
{
    auto counts = std::vector<int> {};

    for (auto count: {2, 4, 8})
        if (Device::shared().supportsSampleCount(count))
            counts.push_back(count);

    return counts;
}
} // namespace

struct AntiAliasingView final : GPUView
{
    AntiAliasingView()
    {
        // The window only ever receives two pictures copied one texel per
        // pixel. Nothing drawn into it has an edge to smooth, so it does not
        // need the 4 samples a GPUView takes by default.
        setSampleCount(1);
        setHandlesMouseEvents(true);

        for (auto* composite: {&leftComposite, &rightComposite})
        {
            composite->setVertices(screenQuad);
            composite->setIndices(quadIndices);
            composite->prepare(sampleCount());
        }

        // Start the right-hand side at 4x.
        for (auto i = 0; i < (int) counts.size(); ++i)
            if (counts[i] <= 4)
                countIndex = i;

        smooth.emplace(counts[countIndex]);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        angle += 0.2f * static_cast<float>(time.delta);
    }

    // A click moves the right-hand side on to the next sample count. That is
    // a new pipeline and a new target: both are built for one count.
    void mouseDown(const Graphics::MouseEvent&) override
    {
        countIndex = (countIndex + 1) % (int) counts.size();
        smooth.emplace(counts[countIndex]);
        paneWidth = 0; // forces resizeTargets() to build its texture
    }

    // Each half of the window is a pane; both scene textures are the size of
    // one pane, in pixels.
    void resizeTargets(int width, int height, float scale)
    {
        if (width == paneWidth && height == paneHeight)
            return;

        paneWidth = width;
        paneHeight = height;

        aliased.resize(width, height);
        smooth->resize(width, height);

        leftComposite.scene = *aliased.target;
        rightComposite.scene = *smooth->target;

        // The inset fills the bottom-right corner; it magnifies a spot on the
        // fan where the spokes are about a pixel wide and nearly horizontal.
        auto half = std::floor(0.22f * (float) std::min(width, height));
        auto margin = 12.0f * scale;

        for (auto* composite: {&leftComposite, &rightComposite})
        {
            composite->size = Vec2 {(float) width, (float) height};
            composite->focus = Vec2 {0.14f * (float) width, 0.58f * (float) height};
            composite->insetCenter =
                Vec2 {(float) width - margin - half, (float) height - margin - half};
            composite->insetHalf = half;
            composite->zoom = 6.0f;
            composite->frameWidth = std::round(scale);
            composite->dotSize = std::round(5.0f * scale);
        }

        leftComposite.samples = (float) aliased.sampleCount;
        rightComposite.samples = (float) smooth->sampleCount;
    }

    void render(Frame& frame) override
    {
        auto size = frame.pixelSize();
        auto width = (int) size.x;
        auto height = (int) size.y;
        auto scale = frame.backingScale();

        // A gap of a few points between the two panes, left the clear colour.
        auto gap = (int) std::round(4.0f * scale);
        auto pane = (width - gap) / 2;

        if (pane <= 0 || height <= 0)
            return;

        resizeTargets(pane, height, scale);

        // The scene twice, into the two textures. Each multisampled pass
        // resolves into its texture as it ends.
        aliased.draw(frame, angle);
        smooth->draw(frame, angle);

        // Then both pictures into the window, one per viewport.
        auto pass = frame.beginPass({Graphics::Color {0.5f, 0.5f, 0.5f}});

        pass.setViewport({0.0f, 0.0f, (float) pane, (float) height});
        pass.draw(leftComposite);

        pass.setViewport(
            {(float) (width - pane), 0.0f, (float) pane, (float) height});
        pass.draw(rightComposite);
    }

    std::vector<int> counts = supportedCounts();
    int countIndex = 0;

    // The scenes own the textures, and are declared before the composite
    // shaders that point at them, so they outlive them.
    Scene aliased {1};
    std::optional<Scene> smooth;

    CompositeShader leftComposite;
    CompositeShader rightComposite;

    int paneWidth = 0;
    int paneHeight = 0;
    float angle = 0.6f;
};
} // namespace AntiAliasing

LEARNEACP_CHAPTER(AntiAliasing::AntiAliasingView)
