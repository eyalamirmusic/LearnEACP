// 16. Framebuffers: the lit scene is rendered into an offscreen colour texture
// with its own depth buffer, and that texture is then drawn over the whole
// window by a post-processing shader. Six effects (none, inversion, grayscale,
// sharpen, blur, edge detection) take turns every few seconds; a mouse click
// moves on to the next one straight away.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

#include <array>
#include <cmath>
#include <optional>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace Framebuffers
{
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

// The full-screen quad: a clip-space position and the texture coordinate that
// reads the same point of the offscreen image.
struct QuadVertex
{
    Vec2 position;
    Vec2 uv;
};

namespace
{
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

// Clip space has y up, but row 0 of a texture is its top, so the corner at
// clip-space y = +1 reads v = 0. No flip is needed anywhere else: the offscreen
// pass writes its top row where this reads it back.
const QuadVertex screenQuad[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}}, // bottom left
    {{1.0f, -1.0f}, {1.0f, 1.0f}}, // bottom right
    {{1.0f, 1.0f}, {1.0f, 0.0f}}, // top right
    {{-1.0f, 1.0f}, {0.0f, 0.0f}}, // top left
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

// Chapter 09's Phong shader, with one addition so the floor has some detail for
// the effects to work on: checker blends a two-tone checkerboard, one world
// unit per square, into the object colour.
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

        auto fragmentPosition = varying(worldPosition);
        auto n = normalize(varying(worldNormal));

        auto l = normalize(lightPosition - fragmentPosition);
        auto v = normalize(cameraPosition - fragmentPosition);
        auto r = reflect(-l, n);

        auto ambient = 0.15f;
        auto diffuse = max(dot(n, l), 0.0f);
        auto specular = 0.5f * pow(max(dot(v, r), 0.0f), 32.0f);

        // 0 or 1, alternating from one unit square to the next.
        auto square =
            mod(floor(fragmentPosition.x()) + floor(fragmentPosition.z()), 2.0f);
        auto albedo = objectColor * (1.0f - checker * 0.45f * square);

        setFragment(float4((ambient + diffuse) * albedo + specular, 1.0f));
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> lightPosition;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> objectColor;
    Uniform<Float> checker;

    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                lightPosition,
                cameraPosition,
                objectColor,
                checker)
};

enum class Effect
{
    None,
    Inversion,
    Grayscale,
    Sharpen,
    Blur,
    EdgeDetection,
};

constexpr auto effectCount = 6;

// 3x3 convolution kernels, row by row from the top-left neighbour. The sharpen
// and edge kernels sum to 1 and 0; the blur sums to 1 (16 sixteenths).
using Kernel = std::array<float, 9>;

constexpr Kernel sharpenKernel = {-1, -1, -1, -1, 9, -1, -1, -1, -1};

constexpr Kernel blurKernel = {1.0f / 16,
                               2.0f / 16,
                               1.0f / 16,
                               2.0f / 16,
                               4.0f / 16,
                               2.0f / 16,
                               1.0f / 16,
                               2.0f / 16,
                               1.0f / 16};

constexpr Kernel edgeKernel = {1, 1, 1, 1, -8, 1, 1, 1, 1};

// The post-processing pass. Each effect is its own ShaderProgram: the Effect
// passed to the constructor is ordinary C++, and define() uses it to decide
// which graph to record, so each compiled shader holds one effect and nothing
// else.
struct PostShader final : ShaderProgram
{
    explicit PostShader(Effect effectToUse)
        : effect(effectToUse)
    {
        // The image is drawn one texel per pixel, so nearest filtering is
        // exact. Clamp makes the kernels repeat the border texel instead of
        // wrapping round to the opposite edge.
        scene.sampling = {TextureFilter::Nearest, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));

        setPosition(float4(position, 0.0f, 1.0f));
        setFragment(float4(apply(uv), 1.0f));
    }

    Float3 apply(const Float2& uv)
    {
        auto color = sample(scene, uv).xyz();

        switch (effect)
        {
            case Effect::None:
                return color;

            case Effect::Inversion:
                return 1.0f - color;

            case Effect::Grayscale:
            {
                // Weighted by how bright each primary looks to the eye: green
                // most, blue least.
                auto luma = dot(color, float3(constant(0.2126f), 0.7152f, 0.0722f));
                return float3(luma, luma, luma);
            }

            case Effect::Sharpen:
                return convolve(uv, sharpenKernel);

            case Effect::Blur:
                return convolve(uv, blurKernel);

            case Effect::EdgeDetection:
                return convolve(uv, edgeKernel);
        }

        return color;
    }

    // Nine samples around uv, each weighted by its kernel entry and summed.
    // The loops are C++, so they run while the graph is recorded and leave
    // nine sample() calls in the shader, with the weights as literals.
    Float3 convolve(const Float2& uv, const Kernel& kernel)
    {
        auto sum = float3(constant(0.0f), 0.0f, 0.0f);

        for (auto row = 0; row < 3; ++row)
        {
            for (auto column = 0; column < 3; ++column)
            {
                // -1, 0 or +1 neighbour steps; v grows downwards, like row.
                auto dx = static_cast<float>(column - 1);
                auto dy = static_cast<float>(row - 1);
                auto offset = float2(texelOffset.x() * dx, texelOffset.y() * dy);

                sum = sum
                      + sample(scene, uv + offset).xyz() * kernel[row * 3 + column];
            }
        }

        return sum;
    }

    Effect effect;

    Uniform<Texture2D> scene;
    Uniform<Float2> texelOffset; // one neighbour step, in UV units

    EACP_SHADER(scene, texelOffset)
};
} // namespace

struct FramebuffersView final : GPUView
{
    FramebuffersView()
    {
        // The pipeline that draws into the offscreen texture. It must match
        // that target, not the window: one sample, the texture's colour format,
        // and a depth attachment because the texture will have one.
        auto scenePipeline = RenderPipelineDescriptor {};
        scenePipeline.sampleCount = 1;
        scenePipeline.depth = true;
        scenePipeline.cullMode = CullMode::Back;
        scenePipeline.colorFormat = pixelFormatFor(targetFormat);

        litShader.setVertices(cube.vertices);
        litShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        litShader.prepare(scenePipeline);

        // The quad draws into the window, which has no depth buffer: a
        // full-screen image has nothing to hide behind.
        for (auto& post: postShaders)
        {
            post.setVertices(screenQuad);
            post.setIndices(quadIndices);
            post.prepare(sampleCount());
        }

        setHandlesMouseEvents(true);
        setContinuous(true);
    }

    // A texture's size is fixed when it is made, so a new view size means a
    // new texture. Called from render() with the size of this frame's
    // drawable, in pixels, and its pixels per logical point.
    void resizeTarget(int width, int height, float scale)
    {
        if (target && width == targetWidth && height == targetHeight)
            return;

        targetWidth = width;
        targetHeight = height;

        auto descriptor = TextureDescriptor {};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.format = targetFormat;
        descriptor.renderTarget = true; // a pass can draw into it
        descriptor.depth = true; // and that pass gets a depth buffer of its own

        target.emplace(Device::shared().makeTexture(descriptor));

        // One neighbour step is one logical point: a texel on a standard
        // display, two on a Retina one, so the effects look the same on both.
        for (auto& post: postShaders)
        {
            post.scene = *target;
            post.texelOffset = Vec2 {scale / (float) width, scale / (float) height};
        }
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        cameraAngle += 0.15f * delta;
        effectTime += delta;

        if (effectTime > secondsPerEffect)
            nextEffect();
    }

    void mouseDown(const Graphics::MouseEvent&) override { nextEffect(); }

    void nextEffect()
    {
        effectIndex = (effectIndex + 1) % effectCount;
        effectTime = 0.0f;
    }

    void render(Frame& frame) override
    {
        auto size = frame.pixelSize();
        auto width = (int) size.x;
        auto height = (int) size.y;

        if (width <= 0 || height <= 0)
            return;

        resizeTarget(width, height, frame.backingScale());

        // First pass: the scene, into the texture. The pass ends at the
        // closing brace, and only then may a later pass sample what it drew.
        {
            auto pass =
                frame.beginPass(*target, {Graphics::Color {0.55f, 0.7f, 0.85f}});
            drawScene(pass, (float) width / (float) height);
        }

        // Second pass: the texture, through the current effect, into the window.
        auto pass = frame.beginPass();
        pass.draw(postShaders[effectIndex]);
    }

    // Three cubes on a checkered floor, one point light, a camera circling
    // slowly. The same lit shader draws all four objects; its uniforms are
    // copied into the pass at each draw, so changing them between draws is fine.
    void drawScene(RenderPass& pass, float aspect)
    {
        auto cameraPosition =
            Vec3 {6.0f * std::sin(cameraAngle), 3.0f, 6.0f * std::cos(cameraAngle)};

        auto view = Mat4::lookAt(cameraPosition, {0.0f, 0.5f, 0.0f}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);

        litShader.viewProjection = projection * view;
        litShader.lightPosition = Vec3 {2.0f, 4.0f, 2.5f};
        litShader.cameraPosition = cameraPosition;

        auto drawObject = [&](const Mat4& model, Vec3 color, float checker)
        {
            litShader.model = model;
            litShader.normalMatrix = model.inverted().transposed();
            litShader.objectColor = color;
            litShader.checker = checker;
            pass.draw(litShader);
        };

        // The floor: a cube flattened to a slab whose top face is at y = 0.
        drawObject(Mat4::translation({0.0f, -0.05f, 0.0f})
                       * Mat4::scale({12.0f, 0.1f, 12.0f}),
                   {0.85f, 0.85f, 0.8f},
                   1.0f);

        drawObject(Mat4::translation({-1.2f, 0.5f, 0.3f}) * Mat4::rotationY(0.4f),
                   {1.0f, 0.5f, 0.31f},
                   0.0f);

        drawObject(Mat4::translation({1.1f, 0.35f, 0.9f}) * Mat4::rotationY(-0.3f)
                       * Mat4::scale(0.7f),
                   {0.3f, 0.6f, 0.9f},
                   0.0f);

        drawObject(Mat4::translation({0.4f, 0.75f, -1.4f}) * Mat4::rotationY(0.9f)
                       * Mat4::scale(1.5f),
                   {0.45f, 0.8f, 0.4f},
                   0.0f);
    }

    static constexpr auto targetFormat = TextureFormat::RGBA8Unorm;
    static constexpr auto secondsPerEffect = 3.0f;

    CubeMesh cube = makeCube();
    LitShader litShader;

    // Declared before the shaders that point at it, so it outlives them.
    std::optional<Texture> target;
    int targetWidth = 0;
    int targetHeight = 0;

    std::array<PostShader, effectCount> postShaders {
        PostShader {Effect::None},
        PostShader {Effect::Inversion},
        PostShader {Effect::Grayscale},
        PostShader {Effect::Sharpen},
        PostShader {Effect::Blur},
        PostShader {Effect::EdgeDetection}};

    int effectIndex = 0;
    float effectTime = 0.0f;
    float cameraAngle = 0.0f;
};
} // namespace Framebuffers

LEARNEACP_CHAPTER(Framebuffers::FramebuffersView)
