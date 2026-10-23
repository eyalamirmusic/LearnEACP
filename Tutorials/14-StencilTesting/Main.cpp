// 14. Stencil Testing: three cubes on a checkered floor, each drawn with a
// glowing outline. The cubes write 1 into the stencil buffer where they cover
// the screen; slightly larger copies are then drawn in a flat colour wherever
// the stencil is *not* 1, which leaves only a rim around each cube.
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

namespace
{
struct CubeMesh
{
    std::array<Vertex, 24> vertices;
    std::array<std::uint16_t, 36> indices;
};

// The unit cube from chapter 09: six faces of four vertices each, wound
// counter-clockwise seen from outside, every corner carrying its face's normal.
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

// The floor: one square in the y = 0 plane, facing up, wound counter-clockwise
// seen from above.
constexpr auto floorSize = 8.0f;

const std::array<Vertex, 4> floorVertices = {{
    {{-floorSize, 0, floorSize}, {0, 1, 0}},
    {{floorSize, 0, floorSize}, {0, 1, 0}},
    {{floorSize, 0, -floorSize}, {0, 1, 0}},
    {{-floorSize, 0, -floorSize}, {0, 1, 0}},
}};

const std::array<std::uint16_t, 6> floorIndices = {0, 1, 2, 0, 2, 3};

// Chapter 09's Phong shader, plus an optional checkerboard so the floor reads
// as a surface. `checker` is 0 for the cubes and 1 for the floor.
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

        // One-unit squares on the xz plane: the sum of the two cell indices is
        // even on one colour and odd on the other, and fract(sum / 2) tells
        // the two apart as 0 or 0.5.
        auto cells = floor(fragmentPosition.xz());
        auto odd = fract((cells.x() + cells.y()) * 0.5f) * 2.0f;
        auto pattern = mix(1.0f, 0.55f + 0.45f * odd, checker);

        auto l = normalize(lightPosition - fragmentPosition);
        auto v = normalize(cameraPosition - fragmentPosition);
        auto r = reflect(-l, n);

        auto ambient = 0.15f;
        auto diffuse = max(dot(n, l), 0.0f);
        auto specular = 0.4f * pow(max(dot(v, r), 0.0f), shininess);

        auto lit = (ambient + diffuse) * objectColor * pattern + specular;
        setFragment(float4(lit, 1.0f));
    }

    static constexpr float shininess = 32.0f;

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

// The outline: the same cube, unlit, in one flat colour.
struct FlatShader final : ShaderProgram
{
    FlatShader() { compile(); }

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

// Where each cube stands, how it is turned, how big it is, and its colour. Each
// stands on the floor, so its centre is half its size above y = 0.
struct CubePlacement
{
    Vec3 position;
    float angle;
    float scale;
    Vec3 color;
};

const std::array<CubePlacement, 3> cubes = {{
    {{-1.5f, 0.5f, -0.4f}, 0.3f, 1.0f, {1.0f, 0.5f, 0.31f}},
    {{1.3f, 0.6f, -1.1f}, -0.5f, 1.2f, {0.35f, 0.6f, 1.0f}},
    {{0.3f, 0.35f, 1.3f}, 0.9f, 0.7f, {0.45f, 0.85f, 0.4f}},
}};
} // namespace

struct StencilTestingView final : GPUView
{
    StencilTestingView()
    {
        // A stencil plane, which also brings the depth plane with it: the two
        // live together in one attachment.
        setStencil(true);

        // Every pipeline drawing into a pass with a stencil plane must say so,
        // even one that never touches the stencil, because the attachment's
        // format is part of what a pipeline is built for.
        auto base = RenderPipelineDescriptor {};
        base.sampleCount = sampleCount();
        base.depth = true;
        base.stencil = true;
        base.cullMode = CullMode::Back;

        // The floor: depth tested as usual. Its stencil faces keep their
        // defaults (always pass, keep the value), so it leaves the stencil
        // buffer alone.
        floorShader.setVertices(floorVertices);
        floorShader.setIndices(floorIndices.data(), (int) floorIndices.size());
        floorShader.prepare(base);

        // The cubes: wherever a cube's fragment survives both tests, replace
        // the stencil value with the pass's reference value, 1.
        auto marking = base;
        marking.stencilFront.compare = CompareFunction::Always;
        marking.stencilFront.pass = StencilOp::Replace;
        marking.stencilBack = marking.stencilFront;

        cubeShader.setVertices(cube.vertices);
        cubeShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        cubeShader.prepare(marking);

        // The outlines: drawn only where the stencil is not 1, i.e. outside
        // every cube, and with the depth test switched off (always pass, write
        // nothing) so the rim shows even where the floor or another cube is
        // nearer. `depth` stays true: the pass has a depth plane, and the
        // pipeline has to agree with it.
        auto outlining = base;
        outlining.depthCompare = CompareFunction::Always;
        outlining.depthWrite = false;
        outlining.stencilFront.compare = CompareFunction::NotEqual;
        outlining.stencilBack = outlining.stencilFront;
        // Its three ops are all Keep already; a write mask of zero makes "the
        // outline pass never changes the stencil" a guarantee, not a default.
        outlining.stencilWriteMask = 0x00;

        outlineShader.setVertices(cube.vertices);
        outlineShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        outlineShader.prepare(outlining);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        cameraAngle += 0.15f * delta;
        spin += 0.5f * delta;
    }

    void render(Frame& frame) override
    {
        // The camera circles the scene slowly, looking slightly down at it.
        auto cameraPosition =
            Vec3 {6.0f * std::sin(cameraAngle), 3.2f, 6.0f * std::cos(cameraAngle)};
        auto lightPosition = Vec3 {2.0f, 5.0f, 2.5f};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 0.4f, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        auto setCommon = [&](LitShader& shader)
        {
            shader.viewProjection = viewProjection;
            shader.lightPosition = lightPosition;
            shader.cameraPosition = cameraPosition;
        };

        setCommon(floorShader);
        setCommon(cubeShader);

        // Zero everywhere to start with: "no cube here".
        auto descriptor = RenderPassDescriptor {};
        descriptor.clearColor = Graphics::Color {0.06f, 0.06f, 0.08f};
        descriptor.clearStencil = 0;

        auto pass = frame.beginPass(descriptor);

        // The value Replace writes and NotEqual compares against.
        pass.setStencilReference(1);

        // 1. The floor, leaving the stencil at 0.
        floorShader.model = Mat4 {};
        floorShader.normalMatrix = Mat4 {};
        floorShader.objectColor = Vec3 {0.8f, 0.8f, 0.8f};
        floorShader.checker = 1.0f;
        pass.draw(floorShader);

        // 2. The cubes, marking every pixel they cover with a 1. The same
        // program is drawn three times; each draw takes a copy of the uniforms
        // as they are at that moment.
        cubeShader.checker = 0.0f;

        for (auto i = 0; i < (int) cubes.size(); ++i)
        {
            auto model = cubeModel(i);
            cubeShader.model = model;
            cubeShader.normalMatrix = model.inverted().transposed();
            cubeShader.objectColor = cubes[i].color;
            pass.draw(cubeShader);
        }

        // 3. The outlines: each cube again, scaled up about its own centre, in
        // a flat colour. Inside the cubes the stencil is 1 and the test throws
        // the fragment away; only the rim that sticks out survives.
        outlineShader.color = Vec3 {1.0f, 0.78f, 0.2f};

        for (auto i = 0; i < (int) cubes.size(); ++i)
        {
            auto model = cubeModel(i) * Mat4::scale(outlineScale);
            outlineShader.modelViewProjection = viewProjection * model;
            pass.draw(outlineShader);
        }
    }

    // T * R * S as in chapter 07; the blue cube also turns slowly.
    Mat4 cubeModel(int index) const
    {
        const auto& placement = cubes[index];
        auto angle = placement.angle + (index == 1 ? spin : 0.0f);

        return Mat4::translation(placement.position) * Mat4::rotationY(angle)
               * Mat4::scale(placement.scale);
    }

    static constexpr float outlineScale = 1.08f;

    CubeMesh cube = makeCube();
    LitShader floorShader;
    LitShader cubeShader;
    FlatShader outlineShader;
    float cameraAngle = 0.0f;
    float spin = 0.0f;
};

int main()
{
    return Graphics::runWindowedApp<StencilTestingView>();
}
