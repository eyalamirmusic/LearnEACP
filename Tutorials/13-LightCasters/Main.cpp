// 13. Light Casters: three kinds of light in one fragment shader. A dim
// directional "moonlight", four coloured point lights that fade with distance,
// and a spotlight with a soft-edged cone that sweeps across a field of cubes.
// Each light type is a C++ helper function that returns its contribution, and
// the point lights are read from a storage buffer the fragment stage indexes.
#include <eacp/GPU/GPU.h>

#include <array>
#include <cmath>

using namespace eacp;
using namespace GPU;
using namespace Maths;

struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

// One point light as the GPU reads it: nine floats, three Vec3s back to back.
// The fragment shader reads the buffer as a flat array of floats, so the record
// needs no padding; record i starts at Vec3 number 3 * i.
struct PointLight
{
    Vec3 position;
    Vec3 color;
    Vec3 attenuation; // constant, linear, quadratic
};

static_assert(sizeof(PointLight) == 9 * sizeof(float));

namespace
{
// Shared by the CPU, which fills the buffer, and the shader, which reads it.
constexpr auto pointLightCount = 4u;

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

// Everything the lighting helpers need to know about the point being shaded.
// The EDSL has no struct type of its own, and needs none: a struct of handles
// is just a C++ struct.
struct Surface
{
    Float3 position;
    Float3 normal;
    Float3 toEye;
    Float3 albedo;
};

constexpr float shininess = 32.0f;
constexpr float specularStrength = 0.5f;

// Diffuse plus specular for one light, given the unit direction towards it and
// the light that actually arrives (its colour, already dimmed by distance or by
// a cone). Every light type below ends in a call to this.
Float3 phong(const Surface& surface, const Float3& toLight, const Float3& arriving)
{
    auto diffuse = max(dot(surface.normal, toLight), 0.0f);
    auto r = reflect(-toLight, surface.normal);
    auto specular =
        specularStrength * pow(max(dot(surface.toEye, r), 0.0f), shininess);

    return (diffuse * surface.albedo + specular) * arriving;
}

// 1 / (constant + linear * d + quadratic * d^2): 1 right at the light, falling
// off quickly at first and then slowly, never quite reaching zero.
Float attenuation(const Float& distance, const Float3& coefficients)
{
    return 1.0f
           / (coefficients.x() + coefficients.y() * distance
              + coefficients.z() * distance * distance);
}

// A light so far away that its rays are parallel: one direction for the whole
// scene, no position and no fall-off.
Float3 directionalLight(const Surface& surface,
                        const Float3& direction,
                        const Float3& color)
{
    return phong(surface, normalize(-direction), color);
}

// A light at a point, shining equally in every direction and fading with
// distance.
Float3 pointLight(const Surface& surface,
                  const Float3& position,
                  const Float3& color,
                  const Float3& coefficients)
{
    auto toLight = position - surface.position;
    auto distance = length(toLight);

    return phong(
        surface, toLight / distance, color * attenuation(distance, coefficients));
}

// A point light that only shines inside a cone. cutoffs holds the cosines of
// the inner and outer cone angles: full strength inside the inner one, nothing
// outside the outer one, and a smooth ramp between them.
Float3 spotLight(const Surface& surface,
                 const Float3& position,
                 const Float3& direction,
                 const Float3& color,
                 const Float3& coefficients,
                 const Float2& cutoffs)
{
    auto toLight = position - surface.position;
    auto distance = length(toLight);
    auto l = toLight / distance;

    // The cosine of the angle between the spot's axis and the ray from the
    // spot to this point. Larger means closer to the middle of the cone.
    auto theta = dot(-l, normalize(direction));
    auto cone =
        clamp((theta - cutoffs.y()) / (cutoffs.x() - cutoffs.y()), 0.0f, 1.0f);

    return phong(surface, l, color * (cone * attenuation(distance, coefficients)));
}

// Every lit object in the scene: the ground and the cubes.
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

        auto surface = Surface {
            fragmentPosition,
            normalize(varying(worldNormal)),
            normalize(cameraPosition - fragmentPosition),
            objectColor,
        };

        // A small constant stands in for all the light bouncing around the
        // scene, so nothing is ever completely black.
        auto color = 0.04f * surface.albedo;

        color = color + directionalLight(surface, sunDirection, sunColor);

        // An ordinary C++ loop. It runs once, while define() records the
        // graph, and leaves four calls to pointLight() behind, each reading its
        // own record from the buffer at a fixed index.
        for (auto i = 0u; i < pointLightCount; ++i)
        {
            color = color
                    + pointLight(surface,
                                 pointLights.read3(3 * i),
                                 pointLights.read3(3 * i + 1),
                                 pointLights.read3(3 * i + 2));
        }

        color = color
                + spotLight(surface,
                            spotPosition,
                            spotDirection,
                            spotColor,
                            spotAttenuation,
                            spotCutoffs);

        setFragment(float4(color, 1.0f));
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> objectColor;

    Uniform<Float3> sunDirection;
    Uniform<Float3> sunColor;

    // A storage buffer of PointLight records, bound whole and read by index.
    Uniform<InputBuffer> pointLights;

    Uniform<Float3> spotPosition;
    Uniform<Float3> spotDirection;
    Uniform<Float3> spotColor;
    Uniform<Float3> spotAttenuation;
    Uniform<Float2> spotCutoffs; // cos(inner), cos(outer)

    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                cameraPosition,
                objectColor,
                sunDirection,
                sunColor,
                pointLights,
                spotPosition,
                spotDirection,
                spotColor,
                spotAttenuation,
                spotCutoffs)
};

// The markers that show where the lights are: the same cube, unlit.
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

// The four point lights, low down between the cubes. The attenuation terms
// give each one a reach of roughly 20 units: bright up close, a faint tint
// across the far side of the ground.
const std::array<PointLight, pointLightCount> pointLightData = {{
    {{-4.0f, 1.2f, -1.5f}, {1.0f, 0.25f, 0.2f}, {1.0f, 0.22f, 0.20f}},
    {{3.5f, 1.2f, -3.5f}, {0.25f, 1.0f, 0.35f}, {1.0f, 0.22f, 0.20f}},
    {{-2.0f, 1.2f, 3.5f}, {0.3f, 0.45f, 1.0f}, {1.0f, 0.22f, 0.20f}},
    {{4.5f, 1.2f, 2.5f}, {1.0f, 0.7f, 0.2f}, {1.0f, 0.22f, 0.20f}},
}};

// The cubes standing on the ground: where, how big, and turned how far.
struct Crate
{
    float x, z, size, angle;
};

const Crate crates[] = {
    {0.0f, 0.0f, 1.4f, 0.3f},
    {-2.5f, -3.0f, 1.0f, 0.8f},
    {2.0f, -1.5f, 1.2f, -0.4f},
    {-5.5f, 1.5f, 1.3f, 0.2f},
    {5.5f, -0.5f, 1.0f, 1.1f},
    {1.5f, 3.0f, 0.9f, -0.9f},
    {-3.0f, 0.8f, 0.8f, 0.5f},
    {0.5f, -5.0f, 1.5f, 0.1f},
    {-6.0f, -4.5f, 1.1f, -0.6f},
    {6.0f, -5.0f, 1.3f, 0.7f},
    {-1.0f, 6.0f, 1.0f, 0.4f},
    {4.0f, 5.5f, 1.2f, -0.2f},
    {-5.0f, 5.0f, 0.9f, 1.0f},
    {2.5f, -7.0f, 1.0f, -1.2f},
};
} // namespace

struct LightCastersView final : GPUView
{
    LightCastersView()
        : pointLightBuffer(Device::shared().makeBuffer(
              pointLightData.data(), sizeof(pointLightData), BufferUsage::Storage))
    {
        setDepth(true);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = true;
        pipeline.cullMode = CullMode::Back;

        litShader.setVertices(cube.vertices);
        litShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        litShader.prepare(pipeline);

        lampShader.setVertices(cube.vertices);
        lampShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        lampShader.prepare(pipeline);

        // The lights never move, so the buffer is filled once, above, and
        // bound once here. The shader keeps a reference to it, so the buffer
        // is a member that lives as long as the shader.
        litShader.pointLights = pointLightBuffer;

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        elapsed += static_cast<float>(time.delta);
    }

    // Draws one lit cube with the given model matrix and colour. The uniforms
    // are copied into the command stream at each draw, so the same program can
    // be drawn many times in one pass with different values.
    void drawLit(RenderPass& pass, const Mat4& model, const Vec3& color)
    {
        litShader.model = model;
        litShader.normalMatrix = model.inverted().transposed();
        litShader.objectColor = color;
        pass.draw(litShader);
    }

    void drawLamp(RenderPass& pass,
                  const Mat4& viewProjection,
                  const Vec3& position,
                  const Vec3& color)
    {
        auto model = Mat4::translation(position) * Mat4::scale(0.2f);
        lampShader.modelViewProjection = viewProjection * model;
        lampShader.color = color;
        pass.draw(lampShader);
    }

    void render(Frame& frame) override
    {
        auto cameraPosition = Vec3 {0.0f, 8.0f, 13.0f};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 1, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        // The spotlight hangs above the back of the scene, and its aim
        // traces a slow figure-of-eight across the ground.
        auto spotPosition = Vec3 {0.0f, 5.5f, -4.0f};
        auto spotTarget = Vec3 {
            5.5f * std::sin(0.45f * elapsed), 0.0f, 4.5f * std::sin(0.9f * elapsed)};

        litShader.viewProjection = viewProjection;
        litShader.cameraPosition = cameraPosition;

        litShader.sunDirection = Vec3 {-0.4f, -1.0f, -0.6f};
        litShader.sunColor = Vec3 {0.22f, 0.25f, 0.38f};

        litShader.spotPosition = spotPosition;
        litShader.spotDirection = spotTarget - spotPosition;
        litShader.spotColor = Vec3 {1.0f, 0.95f, 0.85f};
        litShader.spotAttenuation = Vec3 {1.0f, 0.0f, 0.004f};
        litShader.spotCutoffs =
            Vec2 {std::cos(radians(14.0f)), std::cos(radians(20.0f))};

        auto pass = frame.beginPass({Graphics::Color {0.02f, 0.02f, 0.03f}});

        // The ground is one more cube, flattened into a slab whose top face
        // sits at y = 0.
        drawLit(pass,
                Mat4::translation({0.0f, -0.1f, 0.0f})
                    * Mat4::scale({18.0f, 0.2f, 18.0f}),
                {0.6f, 0.6f, 0.6f});

        for (const auto& crate: crates)
        {
            auto model = Mat4::translation({crate.x, crate.size * 0.5f, crate.z})
                         * Mat4::rotationY(crate.angle) * Mat4::scale(crate.size);
            drawLit(pass, model, {0.8f, 0.78f, 0.74f});
        }

        for (const auto& light: pointLightData)
            drawLamp(pass, viewProjection, light.position, light.color);

        drawLamp(pass, viewProjection, spotPosition, {1.0f, 0.95f, 0.85f});
    }

    CubeMesh cube = makeCube();
    Buffer pointLightBuffer;
    LitShader litShader;
    LampShader lampShader;
    float elapsed = 0.0f;
};

int main()
{
    return Graphics::runWindowedApp<LightCastersView>();
}
