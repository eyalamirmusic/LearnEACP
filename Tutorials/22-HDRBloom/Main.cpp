// 22. HDR and Bloom: a dark corridor lit by four coloured lights many times
// brighter than 1.0. The scene is rendered into a half-float texture so those
// values survive; the bright parts are extracted into a smaller texture,
// blurred with a separable Gaussian that ping-pongs between two textures, and
// added back before tone mapping and gamma correction. The window is split:
// on the left the HDR image is simply clamped to [0, 1], on the right it is
// bloomed and tone mapped. Drag to move the divider, scroll to change the
// exposure.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace HDRBloom
{
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

struct QuadVertex
{
    Vec2 position;
    Vec2 uv;
};

// One light, as the fragment stage reads it from a storage buffer (chapter
// 13): six floats, no padding. The colour carries the intensity, so a light
// of {12, 2, 1} is twelve times brighter in red than anything a texture or a
// window could hold.
struct PointLight
{
    Vec3 position;
    Vec3 color;
};

static_assert(sizeof(PointLight) == 6 * sizeof(float));

namespace
{
constexpr auto lightCount = 4u;

const std::array<PointLight, lightCount> lights = {{
    {{1.2f, 0.6f, -2.5f}, {12.0f, 1.5f, 0.8f}}, // red
    {{-1.3f, 2.2f, -6.5f}, {1.0f, 2.5f, 14.0f}}, // blue
    {{1.0f, 1.9f, -11.0f}, {1.5f, 10.0f, 2.0f}}, // green
    {{0.0f, 1.3f, -16.0f}, {15.0f, 11.0f, 6.0f}}, // warm white
}};

struct CubeMesh
{
    std::array<Vertex, 24> vertices;
    std::array<std::uint16_t, 36> indices;
};

// The unit cube from chapter 09, face by face, counter-clockwise from outside.
CubeMesh makeCube()
{
    struct Face
    {
        Vec3 normal, across1, across2;
    };

    const Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };

    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    auto mesh = CubeMesh {};

    for (auto f = 0; f < 6; ++f)
    {
        const auto& face = faces[f];

        for (auto c = 0; c < 4; ++c)
        {
            auto position = (face.normal + face.across1 * corners[c][0]
                             + face.across2 * corners[c][1])
                            * 0.5f;
            mesh.vertices[f * 4 + c] = {position, face.normal};
        }

        auto first = static_cast<std::uint16_t>(f * 4);
        const std::uint16_t quad[] = {0, 1, 2, 0, 2, 3};

        for (auto i = 0; i < 6; ++i)
            mesh.indices[f * 6 + i] = first + quad[i];
    }

    return mesh;
}

// Chapter 16's full-screen quad: clip-space y = +1 reads v = 0.
const QuadVertex screenQuad[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{1.0f, -1.0f}, {1.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-1.0f, 1.0f}, {0.0f, 0.0f}},
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

// The lit walls, floor and crates. Diffuse plus specular from every light,
// falling off with the square of the distance as real light does. Nothing
// clamps the sum: a wall right next to a light of intensity 12 comes out far
// above 1, and the half-float target keeps it.
struct LitShader final : ShaderProgram
{
    LitShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * float4(worldPosition, 1.0f));

        auto p = varying(worldPosition);
        auto n = normalize(varying(worldNormal));
        auto v = normalize(cameraPosition - p);

        // A faint checkerboard, one unit per square, so the floor and walls
        // have some detail to light.
        auto square = mod(floor(p.x()) + floor(p.y()) + floor(p.z()), 2.0f);
        auto albedo = objectColor * (1.0f - 0.3f * square);

        auto color = 0.01f * albedo;

        for (auto i = 0u; i < lightCount; ++i)
        {
            auto toLight = lights.read3(2 * i) - p;
            auto distance = length(toLight);
            auto l = toLight / distance;
            auto arriving =
                lights.read3(2 * i + 1) / (1.0f + 2.0f * distance * distance);

            auto diffuse = max(dot(n, l), 0.0f);
            auto r = reflect(-l, n);
            auto specular = 0.3f * pow(max(dot(v, r), 0.0f), 32.0f);

            color = color + (diffuse * albedo + specular) * arriving;
        }

        setFragment(float4(color, 1.0f));
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> objectColor;
    Uniform<InputBuffer> lights;

    EACP_SHADER(
        model, normalMatrix, viewProjection, cameraPosition, objectColor, lights)
};

// The lights themselves: small unlit cubes in the light's full HDR colour.
struct LampShader final : ShaderProgram
{
    LampShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        setPosition(modelViewProjection * float4(position, 1.0f));
        setFragment(float4(color, 1.0f));
    }

    Uniform<Float4x4> modelViewProjection;
    Uniform<Float3> color;

    EACP_SHADER(modelViewProjection, color)
};

// Rec. 709 luminance: how bright a colour looks, as one number. Written out
// rather than as a dot() with a float3 of weights, because a free function has
// no ShaderProgram to make a constant() handle from.
Float luminance(const Float3& color)
{
    return 0.2126f * color.x() + 0.7152f * color.y() + 0.0722f * color.z();
}

// Pass 2: keep only what is brighter than the threshold. Drawn into a target
// half the size of the scene, so each output pixel sits exactly between four
// scene texels, and linear filtering averages those four for free.
struct BrightShader final : ShaderProgram
{
    BrightShader()
    {
        scene.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));
        setPosition(float4(position, 0.0f, 1.0f));

        auto color = sample(scene, uv).xyz();
        auto brightness = luminance(color);

        // Scale the colour by how far above the threshold it is, as a
        // fraction of its brightness: 0 at or below the threshold, rising
        // smoothly towards 1 for very bright pixels. A hard cut-off would make
        // the glow flicker on and off along an edge.
        auto keep = max(brightness - threshold, 0.0f) / max(brightness, 0.0001f);
        setFragment(float4(color * keep, 1.0f));
    }

    Uniform<Texture2D> scene;
    Uniform<Float> threshold;

    EACP_SHADER(scene, threshold)
};

constexpr auto blurRadius = 10;

// The weights of a 1D Gaussian, from the centre outwards, scaled so that the
// whole kernel (the centre once, every other weight twice) sums to 1.
std::array<float, blurRadius + 1> gaussianWeights(float sigma)
{
    auto weights = std::array<float, blurRadius + 1> {};
    auto total = 0.0f;

    for (auto i = 0; i <= blurRadius; ++i)
    {
        weights[i] = std::exp(-(float) (i * i) / (2.0f * sigma * sigma));
        total += i == 0 ? weights[i] : 2.0f * weights[i];
    }

    for (auto& weight: weights)
        weight /= total;

    return weights;
}

// Pass 3: one axis of the separable blur. The same program blurs
// horizontally or vertically depending on the step it is given: one texel
// across, or one texel down.
struct BlurShader final : ShaderProgram
{
    BlurShader()
    {
        source.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));
        setPosition(float4(position, 0.0f, 1.0f));

        // Computed in C++ while the graph is recorded: the loop below unrolls
        // into 21 samples, each with its weight as a literal.
        auto weights = gaussianWeights(5.0f);
        auto sum = sample(source, uv).xyz() * weights[0];

        for (auto i = 1; i <= blurRadius; ++i)
        {
            auto offset = texelStep * (float) i;
            sum = sum + sample(source, uv + offset).xyz() * weights[i];
            sum = sum + sample(source, uv - offset).xyz() * weights[i];
        }

        setFragment(float4(sum, 1.0f));
    }

    Uniform<Texture2D> source;
    Uniform<Float2> texelStep;

    EACP_SHADER(source, texelStep)
};

// Pass 4, into the window: bloom added back, exposure tone mapping and gamma
// correction on the right of the divider; the same HDR image simply clamped
// to [0, 1], with no bloom, on the left.
struct CompositeShader final : ShaderProgram
{
    CompositeShader()
    {
        scene.sampling = {TextureFilter::Nearest, TextureAddressMode::Clamp};
        bloom.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));
        setPosition(float4(position, 0.0f, 1.0f));

        auto hdr = sample(scene, uv).xyz();

        // Left: what an 8-bit target does to the HDR image. Everything above
        // 1 is cut off.
        auto clipped = clamp(hdr, 0.0f, 1.0f);

        // Right: add the blurred bright parts (upsampled by the linear
        // filter), then squeeze [0, infinity) into [0, 1) with the exposure
        // curve.
        auto withBloom = hdr + sample(bloom, uv).xyz() * bloomStrength;
        auto mapped = 1.0f - exp(-withBloom * exposure);

        auto linear = select(uv.x() < split, clipped, mapped);

        // Both halves are linear light; the display expects gamma-encoded
        // values, so encode them.
        auto encoded = pow(linear, 1.0f / 2.2f);

        // A thin line marks the divider.
        auto onLine = abs(uv.x() - split) < lineHalfWidth;
        auto color = select(onLine, float3(constant(0.8f), 0.8f, 0.8f), encoded);

        setFragment(float4(color, 1.0f));
    }

    Uniform<Texture2D> scene;
    Uniform<Texture2D> bloom;
    Uniform<Float> exposure;
    Uniform<Float> bloomStrength;
    Uniform<Float> split; // the divider, as a u coordinate
    Uniform<Float> lineHalfWidth; // in u units

    EACP_SHADER(scene, bloom, exposure, bloomStrength, split, lineHalfWidth)
};
} // namespace

struct HDRBloomView final : GPUView
{
    HDRBloomView()
        : lightBuffer(Device::shared().makeBuffer(
              lights.data(), sizeof(lights), BufferUsage::Storage))
    {
        setHandlesMouseEvents(true);

        // Every pass but the last draws into an RGBA16Float texture, so every
        // pipeline but the last's is built for that format, single-sampled.
        auto scenePipeline = RenderPipelineDescriptor {};
        scenePipeline.sampleCount = 1;
        scenePipeline.depth = true;
        scenePipeline.cullMode = CullMode::Back;
        scenePipeline.colorFormat = pixelFormatFor(hdrFormat);

        litShader.setVertices(cube.vertices);
        litShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        litShader.prepare(scenePipeline);
        litShader.lights = lightBuffer;

        lampShader.setVertices(cube.vertices);
        lampShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        lampShader.prepare(scenePipeline);

        auto postPipeline = RenderPipelineDescriptor {};
        postPipeline.sampleCount = 1;
        postPipeline.colorFormat = pixelFormatFor(hdrFormat);

        auto prepareQuad = [&](ShaderProgram& program)
        {
            program.setVertices(screenQuad);
            program.setIndices(quadIndices);
            program.prepare(postPipeline);
        };

        prepareQuad(brightShader);
        prepareQuad(blurHorizontal);
        prepareQuad(blurVertical);

        // The last pass draws into the window, an ordinary 8-bit target.
        compositeShader.setVertices(screenQuad);
        compositeShader.setIndices(quadIndices);
        compositeShader.prepare(sampleCount());

        setContinuous(true);
    }

    static Texture makeTarget(int width, int height, bool depth)
    {
        auto descriptor = TextureDescriptor {};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.format = hdrFormat;
        descriptor.renderTarget = true;
        descriptor.depth = depth;
        return Device::shared().makeTexture(descriptor);
    }

    // The scene target follows the view's size in pixels, as in chapter 16;
    // the two bloom targets are half its size in each direction.
    void resizeTargets(int width, int height, float scale)
    {
        if (sceneTarget && width == targetWidth && height == targetHeight)
            return;

        targetWidth = width;
        targetHeight = height;

        auto bloomWidth = std::max(1, width / 2);
        auto bloomHeight = std::max(1, height / 2);

        sceneTarget.emplace(makeTarget(width, height, true));
        bloomTargets[0].emplace(makeTarget(bloomWidth, bloomHeight, false));
        bloomTargets[1].emplace(makeTarget(bloomWidth, bloomHeight, false));

        // Ping-pong: the bright pass and the vertical blur write bloom 0, the
        // horizontal blur reads it and writes bloom 1, the vertical blur reads
        // that back. Each program always reads the same texture, so these are
        // set once per resize.
        brightShader.scene = *sceneTarget;
        blurHorizontal.source = *bloomTargets[0];
        blurVertical.source = *bloomTargets[1];
        compositeShader.scene = *sceneTarget;
        compositeShader.bloom = *bloomTargets[0];

        // One step is one bloom texel, so the blur is the same width in
        // pixels at any window size.
        blurHorizontal.texelStep = Vec2 {1.0f / (float) bloomWidth, 0.0f};
        blurVertical.texelStep = Vec2 {0.0f, 1.0f / (float) bloomHeight};

        compositeShader.lineHalfWidth = scale / (float) width;
    }

    void mouseDown(const Graphics::MouseEvent& event) override
    {
        moveDivider(event);
    }

    void mouseDragged(const Graphics::MouseEvent& event) override
    {
        moveDivider(event);
    }

    void moveDivider(const Graphics::MouseEvent& event)
    {
        auto bounds = getLocalBounds();

        if (bounds.w > 0.0f)
            split = std::clamp(event.pos.x / bounds.w, 0.0f, 1.0f);
    }

    // Exposure is a multiplier, so scrolling scales it rather than adding to
    // it: each step changes it by the same ratio.
    void mouseWheel(const Graphics::MouseEvent& event) override
    {
        auto step = event.preciseScrolling ? 0.01f : 0.1f;
        exposure =
            std::clamp(exposure * std::exp(event.delta.y * step), 0.05f, 20.0f);
    }

    void update(Threads::FrameTime time) override
    {
        elapsed += static_cast<float>(time.delta);
    }

    void render(Frame& frame) override
    {
        auto size = frame.pixelSize();
        auto width = (int) size.x;
        auto height = (int) size.y;

        if (width <= 0 || height <= 0)
            return;

        resizeTargets(width, height, frame.backingScale());

        // Pass 1: the scene, in HDR.
        {
            auto pass =
                frame.beginPass(*sceneTarget, {Graphics::Color {0.0f, 0.0f, 0.0f}});
            drawScene(pass, (float) width / (float) height);
        }

        // Pass 2: the bright parts, at half size.
        {
            auto pass = frame.beginPass(*bloomTargets[0]);
            brightShader.threshold = 1.0f;
            pass.draw(brightShader);
        }

        // Pass 3: blur, horizontally then vertically, several times over. Each
        // round leaves its result back in bloom 0.
        for (auto i = 0; i < blurRounds; ++i)
        {
            {
                auto pass = frame.beginPass(*bloomTargets[1]);
                pass.draw(blurHorizontal);
            }

            auto pass = frame.beginPass(*bloomTargets[0]);
            pass.draw(blurVertical);
        }

        // Pass 4: combine, tone map and gamma-correct, into the window.
        compositeShader.exposure = exposure;
        compositeShader.bloomStrength = 1.0f;
        compositeShader.split = split;

        auto pass = frame.beginPass();
        pass.draw(compositeShader);
    }

    // A corridor running away from the camera down -z: floor, ceiling and two
    // walls, a few crates, and a small cube at each light.
    void drawScene(RenderPass& pass, float aspect)
    {
        // The camera drifts gently from side to side.
        auto cameraPosition = Vec3 {0.5f * std::sin(0.3f * elapsed), 1.4f, 3.5f};
        auto view = Mat4::lookAt(cameraPosition, {0.0f, 1.2f, -10.0f}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(50.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        litShader.viewProjection = viewProjection;
        litShader.cameraPosition = cameraPosition;

        auto drawLit = [&](const Mat4& model, Vec3 color)
        {
            litShader.model = model;
            litShader.normalMatrix = model.inverted().transposed();
            litShader.objectColor = color;
            pass.draw(litShader);
        };

        // Slabs whose inner faces enclose x in [-2, 2], y in [0, 3], and z
        // from 4 back to -20. Culling keeps only the faces turned towards us,
        // which from inside the corridor are the inner ones.
        auto slab = [&](Vec3 centre, Vec3 extent, Vec3 color)
        { drawLit(Mat4::translation(centre) * Mat4::scale(extent), color); };

        auto stone = Vec3 {0.25f, 0.24f, 0.23f};
        slab({0.0f, -0.1f, -8.0f}, {4.4f, 0.2f, 24.0f}, stone);
        slab({0.0f, 3.1f, -8.0f}, {4.4f, 0.2f, 24.0f}, stone);
        slab({-2.1f, 1.5f, -8.0f}, {0.2f, 3.0f, 24.0f}, stone);
        slab({2.1f, 1.5f, -8.0f}, {0.2f, 3.0f, 24.0f}, stone);
        slab({0.0f, 1.5f, -20.1f}, {4.4f, 3.0f, 0.2f}, stone);

        struct Crate
        {
            float x, z, size, angle;
        };

        const Crate crates[] = {
            {-1.3f, -4.0f, 0.8f, 0.3f},
            {1.2f, -3.0f, 0.7f, -0.4f},
            {1.0f, -9.0f, 1.0f, 0.2f},
            {-1.2f, -13.0f, 0.9f, -0.6f},
            {-0.4f, -17.5f, 1.1f, 0.5f},
        };

        for (const auto& crate: crates)
        {
            drawLit(Mat4::translation({crate.x, crate.size * 0.5f, crate.z})
                        * Mat4::rotationY(crate.angle) * Mat4::scale(crate.size),
                    {0.5f, 0.42f, 0.32f});
        }

        for (const auto& light: lights)
        {
            auto model = Mat4::translation(light.position) * Mat4::scale(0.3f);
            lampShader.modelViewProjection = viewProjection * model;
            lampShader.color = light.color;
            pass.draw(lampShader);
        }
    }

    static constexpr auto hdrFormat = TextureFormat::RGBA16Float;
    static constexpr auto blurRounds = 6;

    CubeMesh cube = makeCube();
    Buffer lightBuffer;

    // Declared before the shaders that point at them, so they outlive them.
    std::optional<Texture> sceneTarget;
    std::array<std::optional<Texture>, 2> bloomTargets;
    int targetWidth = 0;
    int targetHeight = 0;

    LitShader litShader;
    LampShader lampShader;
    BrightShader brightShader;
    BlurShader blurHorizontal;
    BlurShader blurVertical;
    CompositeShader compositeShader;

    float exposure = 1.0f;
    float split = 0.5f;
    float elapsed = 0.0f;
};
} // namespace HDRBloom

LEARNEACP_CHAPTER(HDRBloom::HDRBloomView)
