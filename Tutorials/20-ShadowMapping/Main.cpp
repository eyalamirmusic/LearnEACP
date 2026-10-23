// 20. Shadow Mapping: a field of cubes on a large floor, lit by a sun whose
// direction slowly circles the sky. Every frame first renders the scene from
// the light into a depth-only shadow map, then draws it from the camera, and
// each fragment looks itself up in that map to learn whether something nearer
// the light is in the way. A slope-scaled bias removes shadow acne, and 3x3
// percentage-closer filtering softens the edges. An inset in the corner shows
// the shadow map itself; click the window to hide or show it.
#include <eacp/GPU/GPU.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

using namespace eacp;
using namespace GPU;
using namespace Maths;

// Flip these to see the problems this chapter solves. Without the bias every lit
// surface is striped with shadow acne; without PCF the shadow edges are hard
// and show the shadow map's texels as stair steps.
constexpr bool useSlopeScaledBias = true;
constexpr bool usePercentageCloserFiltering = true;

struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

// The debugging inset: a clip-space position and the UV that reads the same
// point of the shadow map.
struct QuadVertex
{
    Vec2 position;
    Vec2 uv;
};

namespace
{
// The shadow map is square, this many texels a side. The PCF kernel steps one
// texel at a time, so the shader needs the size as well.
constexpr auto shadowMapSize = 2048;

// The light's box, in world units: half its width and height, and how far it
// reaches along the light direction from the light's eye.
constexpr auto lightHalfExtent = 9.0f;
constexpr auto lightNear = 1.0f;
constexpr auto lightFar = 40.0f;
constexpr auto lightDistance = 20.0f;

struct CubeMesh
{
    std::array<Vertex, 24> vertices;
    std::array<std::uint16_t, 36> indices;
};

// The unit cube from chapter 09: 24 vertices, one outward normal per face,
// wound counter-clockwise seen from outside.
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

// A full-viewport quad. Clip-space y = +1 reads v = 0, the top row of the
// shadow map, so the inset shows the map the right way up.
const QuadVertex insetQuad[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{1.0f, -1.0f}, {1.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-1.0f, 1.0f}, {0.0f, 0.0f}},
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

// One box standing on the floor: where its centre is on the ground, how wide
// and how tall it is, and which way it is turned.
struct Box
{
    float x, z;
    float width, height;
    float turn;
    Vec3 color;
};

const Box boxes[] = {
    {0.0f, 0.0f, 1.2f, 3.5f, 0.3f, {0.9f, 0.55f, 0.35f}},
    {-3.0f, 1.5f, 1.4f, 1.2f, -0.4f, {0.35f, 0.6f, 0.9f}},
    {2.8f, -1.8f, 1.0f, 2.2f, 0.8f, {0.45f, 0.8f, 0.45f}},
    {2.5f, 2.6f, 1.6f, 0.8f, 0.1f, {0.85f, 0.8f, 0.4f}},
    {-2.2f, -2.8f, 0.9f, 2.8f, 1.1f, {0.75f, 0.45f, 0.8f}},
    {-5.2f, -0.6f, 1.0f, 1.8f, 0.6f, {0.4f, 0.8f, 0.8f}},
    {5.0f, 0.6f, 1.1f, 1.4f, -0.7f, {0.9f, 0.4f, 0.4f}},
};

// Pass 1: the scene as the light sees it. All we want is the depth buffer, but
// a ShaderProgram always has a fragment stage, so it writes a constant colour
// that the pipeline's colour write mask then throws away.
struct ShadowDepthShader final : ShaderProgram
{
    ShadowDepthShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        setPosition(lightModelViewProjection * float4(position, 1.0f));
        setFragment(float4(constant(0.0f), 0.0f, 0.0f, 1.0f));
    }

    Uniform<Float4x4> lightModelViewProjection;

    EACP_SHADER(lightModelViewProjection)
};

// Pass 2: chapter 09's Phong shader with a directional light, a checkered floor
// as in chapter 16, and the shadow test.
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

        // The same world position, through the light's matrices instead of the
        // camera's: where this vertex landed in the shadow pass.
        auto lightClip = varying(lightViewProjection * float4(worldPosition, 1.0f));

        auto fragmentPosition = varying(worldPosition);
        auto n = normalize(varying(worldNormal));

        // A directional light: the same direction to the light everywhere.
        auto l = normalize(lightDirection);
        auto v = normalize(cameraPosition - fragmentPosition);
        auto r = reflect(-l, n);

        auto nDotL = dot(n, l);
        auto ambient = 0.2f;
        auto diffuse = max(nDotL, 0.0f);
        // Only a face turned towards the light gets a highlight; plain Phong
        // would also light faces turned away from it at grazing angles.
        auto specular = 0.25f * step(0.0f, nDotL) * pow(max(dot(v, r), 0.0f), 32.0f);

        auto shadow = shadowAmount(lightClip, nDotL);
        auto lightAmount = 1.0f - shadow;

        auto square =
            mod(floor(fragmentPosition.x()) + floor(fragmentPosition.z()), 2.0f);
        auto albedo = objectColor * (1.0f - checker * 0.15f * square);

        auto lit =
            (ambient + lightAmount * diffuse) * albedo + lightAmount * specular;
        setFragment(float4(lit, 1.0f));
    }

    // 0 where the light reaches this fragment, 1 where something nearer the
    // light hides it, and in between along a filtered edge.
    Float shadowAmount(const Float4& lightClip, const Float& nDotL)
    {
        // Light clip space to the shadow map. The divide does nothing for an
        // orthographic projection (w is 1), but it is what a spotlight's
        // perspective projection would need, so it stays.
        auto ndc = lightClip.xyz() / lightClip.w();

        // x and y are -1..1 with y up; the shadow map's UVs are 0..1 with v
        // down, row 0 at the top. z is already 0..1, the value the shadow pass
        // wrote, so unlike OpenGL it needs no remapping at all.
        auto uv = float2(ndc.x() * 0.5f + 0.5f, 0.5f - ndc.y() * 0.5f);
        auto depth = ndc.z();

        // Slope-scaled bias: a surface facing the light needs almost none, one
        // the light grazes needs much more, because one shadow-map texel then
        // covers a long slanted strip of it.
        auto bias = useSlopeScaledBias ? max(0.006f * (1.0f - nDotL), 0.0005f)
                                       : constant(0.0f);
        auto testDepth = depth - bias;

        auto shadow = constant(0.0f);

        if (usePercentageCloserFiltering)
        {
            // Nine tests on the texel grid around uv, averaged. The loops are
            // C++, so they unroll into nine sample() calls in the shader.
            constexpr auto texel = 1.0f / static_cast<float>(shadowMapSize);

            for (auto dy = -1; dy <= 1; ++dy)
            {
                for (auto dx = -1; dx <= 1; ++dx)
                {
                    auto offset = float2(constant(dx * texel), dy * texel);
                    auto closest = sample(shadowMap, uv + offset);

                    // step(edge, x) is 1 when x >= edge: this fragment is
                    // further from the light than the closest thing there.
                    shadow = shadow + step(closest, testDepth);
                }
            }

            shadow = shadow / 9.0f;
        }
        else
        {
            shadow = step(sample(shadowMap, uv), testDepth);
        }

        // Outside the light's box the shadow map knows nothing, so call it lit:
        // 1 only when uv is within 0..1 and the depth within the far plane.
        auto inside = step(0.0f, uv.x()) * step(uv.x(), 1.0f) * step(0.0f, uv.y())
                      * step(uv.y(), 1.0f) * step(depth, 1.0f);

        return shadow * inside;
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float4x4> lightViewProjection;
    Uniform<Float3> lightDirection;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> objectColor;
    Uniform<Float> checker;

    // Assigned the shadow pass's render target: its depth plane is what this
    // samples, one Float per read, 0 at the light's near plane and 1 at its far.
    Uniform<TextureDepth2D> shadowMap;

    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                lightViewProjection,
                lightDirection,
                cameraPosition,
                objectColor,
                checker,
                shadowMap)
};

// The inset: the shadow map drawn as grey, near the light dark, far light. An
// orthographic projection spreads depth evenly, so it needs no linearising.
struct DepthInsetShader final : ShaderProgram
{
    DepthInsetShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));

        setPosition(float4(position, 0.0f, 1.0f));

        auto depth = sample(shadowMap, uv);
        setFragment(float4(depth, depth, depth, 1.0f));
    }

    Uniform<TextureDepth2D> shadowMap;

    EACP_SHADER(shadowMap)
};
} // namespace

struct ShadowMappingView final : GPUView
{
    ShadowMappingView()
    {
        setDepth(true);
        setHandlesMouseEvents(true);

        // The shadow map: a render target whose depth plane a later pass may
        // sample. Its colour plane is never written, so it is the smallest
        // format there is.
        auto shadowDescriptor = TextureDescriptor {};
        shadowDescriptor.width = shadowMapSize;
        shadowDescriptor.height = shadowMapSize;
        shadowDescriptor.format = shadowColorFormat;
        shadowDescriptor.renderTarget = true;
        shadowDescriptor.sampleableDepth = true;
        shadowTarget.emplace(Device::shared().makeTexture(shadowDescriptor));

        // Draws into the shadow map: one sample, its colour format, depth on,
        // and no colour writes at all.
        auto shadowPipeline = RenderPipelineDescriptor {};
        shadowPipeline.sampleCount = 1;
        shadowPipeline.depth = true;
        shadowPipeline.cullMode = CullMode::Back;
        shadowPipeline.colorFormat = pixelFormatFor(shadowColorFormat);
        shadowPipeline.colorWriteMask = ColorWriteMask::none();

        depthShader.setVertices(cube.vertices);
        depthShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        depthShader.prepare(shadowPipeline);

        // Draws into the window, as in chapter 09.
        auto scenePipeline = RenderPipelineDescriptor {};
        scenePipeline.sampleCount = sampleCount();
        scenePipeline.depth = true;
        scenePipeline.cullMode = CullMode::Back;

        litShader.setVertices(cube.vertices);
        litShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        litShader.prepare(scenePipeline);
        litShader.shadowMap = *shadowTarget;

        // The inset covers whatever is under it: it ignores the depth buffer
        // and leaves it alone, but still has to say the pass has one.
        auto insetPipeline = RenderPipelineDescriptor {};
        insetPipeline.sampleCount = sampleCount();
        insetPipeline.depth = true;
        insetPipeline.depthCompare = CompareFunction::Always;
        insetPipeline.depthWrite = false;

        insetShader.setVertices(insetQuad);
        insetShader.setIndices(quadIndices);
        insetShader.prepare(insetPipeline);
        insetShader.shadowMap = *shadowTarget;

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        sunAngle += 0.2f * static_cast<float>(time.delta);
    }

    void mouseDown(const Graphics::MouseEvent&) override { showInset = !showInset; }

    void render(Frame& frame) override
    {
        // The direction towards the sun, circling the sky about 40 degrees up.
        auto lightDirection =
            normalize(Vec3 {std::cos(sunAngle), 0.85f, std::sin(sunAngle)});

        // The light's camera: an eye far back along that direction looking at
        // the middle of the scene, and an orthographic box around the scene,
        // because a sun's rays are parallel.
        auto lightView =
            Mat4::lookAt(lightDirection * lightDistance, {0, 0, 0}, {0, 1, 0});
        auto lightProjection = Mat4::orthographic(-lightHalfExtent,
                                                  lightHalfExtent,
                                                  -lightHalfExtent,
                                                  lightHalfExtent,
                                                  lightNear,
                                                  lightFar);
        auto lightViewProjection = lightProjection * lightView;

        // Pass 1: depth from the light. DepthAction::Keep stores the depth
        // plane when the pass ends, so the next pass has something to read.
        {
            auto descriptor = RenderPassDescriptor {};
            descriptor.depthAction = DepthAction::Keep;

            auto pass = frame.beginPass(*shadowTarget, descriptor);
            forEachObject(
                [&](const Mat4& model, Vec3, float)
                {
                    depthShader.lightModelViewProjection =
                        lightViewProjection * model;
                    pass.draw(depthShader);
                });
        }

        // Pass 2: the scene from the camera, testing every fragment against
        // the shadow map.
        auto cameraPosition = Vec3 {9.0f, 8.0f, 12.0f};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0.0f, 0.5f, 0.0f}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);

        litShader.viewProjection = projection * view;
        litShader.lightViewProjection = lightViewProjection;
        litShader.lightDirection = lightDirection;
        litShader.cameraPosition = cameraPosition;

        auto pass = frame.beginPass({Graphics::Color {0.55f, 0.7f, 0.85f}});

        forEachObject(
            [&](const Mat4& model, Vec3 color, float checker)
            {
                litShader.model = model;
                litShader.normalMatrix = model.inverted().transposed();
                litShader.objectColor = color;
                litShader.checker = checker;
                pass.draw(litShader);
            });

        // The inset: a square in the bottom-right corner, in pixels from the
        // top left.
        if (showInset)
        {
            auto size = frame.pixelSize();
            auto side = std::min(size.x, size.y) * 0.3f;
            auto margin = 12.0f * frame.backingScale();

            pass.setViewport(
                {size.x - side - margin, size.y - side - margin, side, side});
            pass.draw(insetShader);
            pass.clearViewport();
        }
    }

    // The floor and every box, as a model matrix, a colour and whether to
    // checker it. Both passes walk the same list, so they draw the same scene.
    template <typename Draw>
    void forEachObject(Draw&& draw)
    {
        // A wide slab whose top is at y = 0, reaching past the light's box.
        draw(Mat4::translation({0.0f, -0.05f, 0.0f})
                 * Mat4::scale({30.0f, 0.1f, 30.0f}),
             Vec3 {0.85f, 0.85f, 0.8f},
             1.0f);

        for (const auto& box: boxes)
        {
            draw(Mat4::translation({box.x, box.height * 0.5f, box.z})
                     * Mat4::rotationY(box.turn)
                     * Mat4::scale({box.width, box.height, box.width}),
                 box.color,
                 0.0f);
        }
    }

    static constexpr auto shadowColorFormat = TextureFormat::R8Unorm;

    CubeMesh cube = makeCube();

    // Declared before the shaders that point at it, so it outlives them.
    std::optional<Texture> shadowTarget;

    ShadowDepthShader depthShader;
    LitShader litShader;
    DepthInsetShader insetShader;

    float sunAngle = 2.4f;
    bool showInset = true;
};

int main()
{
    return Graphics::runWindowedApp<ShadowMappingView>();
}
