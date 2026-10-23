// 09. Lighting: a cube lit per fragment with the Phong model (ambient + diffuse
// + specular) by a point light that orbits it. The light itself is drawn as a
// small white cube so you can see where the light is coming from.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

#include <array>
#include <cmath>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace Lighting
{
// Every vertex now carries a normal: the direction the surface faces at that
// point, of length 1, in the same local space as the position.
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

namespace
{
struct CubeMesh
{
    std::array<Vertex, 24> vertices;
    std::array<std::uint16_t, 36> indices;
};

// A unit cube built face by face. Each face is described by its outward normal
// and two axes across it, chosen so that across1 x across2 = normal: walking
// the corners in the order below is then counter-clockwise seen from outside,
// which is front-facing. All four corners of a face share the face's normal,
// which is why the cube needs 24 vertices and not 8: a corner has three
// different normals, one per face it belongs to.
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

// The lit object. The vertex stage moves positions and normals into world
// space, where the light and the camera are described; the fragment stage
// evaluates the lighting at every pixel.
struct LitShader final : ShaderProgram
{
    LitShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        // Positions are points (w = 1); normals are directions (w = 0), so the
        // translation in the matrix never moves them.
        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * float4(worldPosition, 1.0f));

        // Interpolated across each triangle. A blend of unit vectors is shorter
        // than 1, so the normal is renormalised per fragment.
        auto fragmentPosition = varying(worldPosition);
        auto n = normalize(varying(worldNormal));

        // The three directions Phong needs, all unit length, all pointing away
        // from the surface point: to the light, to the eye, and the light's
        // mirror reflection about the normal.
        auto l = normalize(lightPosition - fragmentPosition);
        auto v = normalize(cameraPosition - fragmentPosition);
        auto r = reflect(-l, n);

        auto ambient = 0.12f;
        auto diffuse = max(dot(n, l), 0.0f);
        auto specular = 0.6f * pow(max(dot(v, r), 0.0f), shininess);

        auto lit = (ambient + diffuse) * objectColor + specular;
        setFragment(float4(lit * lightColor, 1.0f));
    }

    static constexpr float shininess = 32.0f;

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> lightPosition;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> lightColor;
    Uniform<Float3> objectColor;

    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                lightPosition,
                cameraPosition,
                lightColor,
                objectColor)
};

// The lamp: the same cube, unlit, in the light's colour.
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
} // namespace

struct LightingView final : GPUView
{
    LightingView()
    {
        setDepth(true);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = true;
        pipeline.cullMode = CullMode::Back;

        // Both programs draw the same cube, each from its own copy of the
        // buffers, with the same depth-tested, back-face-culled pipeline.
        cubeShader.setVertices(cube.vertices);
        cubeShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        cubeShader.prepare(pipeline);

        lampShader.setVertices(cube.vertices);
        lampShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        lampShader.prepare(pipeline);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        lightAngle += 0.9f * delta;
        cubeAngle += 0.25f * delta;
    }

    void render(Frame& frame) override
    {
        // The light circles the cube a little above it.
        auto lightPosition =
            Vec3 {1.6f * std::cos(lightAngle), 0.7f, 1.6f * std::sin(lightAngle)};
        auto lightColor = Vec3 {1.0f, 1.0f, 1.0f};
        auto cameraPosition = Vec3 {0.0f, 1.4f, 3.6f};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 0, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        auto model = Mat4::rotationY(cubeAngle) * Mat4::rotationX(0.5f);

        cubeShader.model = model;
        // The inverse-transpose keeps normals perpendicular to the surface even
        // under non-uniform scale. The shader language has no inverse(), so the
        // CPU, which does, computes it once per frame.
        cubeShader.normalMatrix = model.inverted().transposed();
        cubeShader.viewProjection = viewProjection;
        cubeShader.lightPosition = lightPosition;
        cubeShader.cameraPosition = cameraPosition;
        cubeShader.lightColor = lightColor;
        cubeShader.objectColor = Vec3 {1.0f, 0.5f, 0.31f};

        auto lampModel = Mat4::translation(lightPosition) * Mat4::scale(0.12f);
        lampShader.modelViewProjection = viewProjection * lampModel;
        lampShader.color = lightColor;

        auto pass = frame.beginPass({Graphics::Color {0.06f, 0.06f, 0.08f}});
        pass.draw(cubeShader);
        pass.draw(lampShader);
    }

    CubeMesh cube = makeCube();
    LitShader cubeShader;
    LampShader lampShader;
    float lightAngle = 0.0f;
    float cubeAngle = 0.0f;
};
} // namespace Lighting

LEARNEACP_CHAPTER(Lighting::LightingView)
