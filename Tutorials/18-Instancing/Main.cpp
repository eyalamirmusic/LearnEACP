// 18. Instancing: an asteroid field of 12,000 rocks in a ring around a planet,
// drawn with one draw call. Every rock is the same small mesh; what makes each
// one different (where it is, how big it is, which way it is turned, its
// colour) comes from a per-instance buffer filled once on the CPU. Press Space
// to switch to one draw call per rock and compare the frame times printed to
// the console.
#include <eacp/GPU/GPU.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace eacp;
using namespace GPU;
using namespace Maths;

// Per vertex, as in chapter 09: a position and a normal in the mesh's own
// local space. Both meshes in this chapter use it.
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

// Per instance: one record for every rock. A vertex attribute is at most four
// floats, so the 4x4 model matrix travels as its four columns and is put back
// together in the shader.
struct Rock
{
    Vec4 model0;
    Vec4 model1;
    Vec4 model2;
    Vec4 model3;
    Vec3 tint;
};

namespace
{
constexpr int rockCount = 12000;
constexpr float ringRadius = 3.4f;
constexpr float ringWidth = 0.75f;

// A small hash turned into a value in 0..1: the same input always gives the
// same output, which is what lets neighbouring triangles agree on how far a
// shared corner was pushed out.
float hashNoise(Vec3 p)
{
    auto h = std::sin(p.x * 127.1f + p.y * 311.7f + p.z * 74.7f) * 43758.5453f;
    return h - std::floor(h);
}

// A lumpy, faceted rock: an icosahedron with every face split into four, each
// corner pushed in or out by the hash above. Every triangle gets its own three
// vertices sharing the face normal, so the rock looks chipped rather than
// smooth. 80 triangles, 240 vertices.
std::vector<Vertex> makeRock()
{
    const auto t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    const Vec3 corners[12] = {
        {-1, t, 0},
        {1, t, 0},
        {-1, -t, 0},
        {1, -t, 0},
        {0, -1, t},
        {0, 1, t},
        {0, -1, -t},
        {0, 1, -t},
        {t, 0, -1},
        {t, 0, 1},
        {-t, 0, -1},
        {-t, 0, 1},
    };

    const int faces[20][3] = {
        {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
        {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
        {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1},
    };

    // A point on the unit sphere, pushed out to between 0.7 and 1.15.
    auto lump = [](Vec3 p)
    {
        auto onSphere = normalize(p);
        return onSphere * (0.7f + 0.45f * hashNoise(onSphere));
    };

    auto vertices = std::vector<Vertex> {};

    auto addTriangle = [&](Vec3 a, Vec3 b, Vec3 c)
    {
        a = lump(a);
        b = lump(b);
        c = lump(c);
        auto normal = normalize(cross(b - a, c - a));

        // Keep every triangle counter-clockwise seen from outside, whatever
        // the lumps did to it, so back-face culling stays correct.
        if (dot(normal, a + b + c) < 0.0f)
        {
            std::swap(b, c);
            normal = -normal;
        }

        vertices.push_back({a, normal});
        vertices.push_back({b, normal});
        vertices.push_back({c, normal});
    };

    for (const auto& face: faces)
    {
        auto a = corners[face[0]];
        auto b = corners[face[1]];
        auto c = corners[face[2]];
        auto ab = (a + b) * 0.5f;
        auto bc = (b + c) * 0.5f;
        auto ca = (c + a) * 0.5f;

        addTriangle(a, ab, ca);
        addTriangle(ab, b, bc);
        addTriangle(ca, bc, c);
        addTriangle(ab, bc, ca);
    }

    return vertices;
}

struct SphereMesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

// A smooth unit sphere for the planet, built from rings of latitude. On a
// unit sphere the normal at a point is the point itself.
SphereMesh makeSphere(int stacks, int slices)
{
    auto mesh = SphereMesh {};
    const auto pi = 3.14159265f;

    for (auto stack = 0; stack <= stacks; ++stack)
    {
        auto polar = pi * (float) stack / (float) stacks;

        for (auto slice = 0; slice <= slices; ++slice)
        {
            auto azimuth = 2.0f * pi * (float) slice / (float) slices;
            auto p = Vec3 {std::sin(polar) * std::cos(azimuth),
                           std::cos(polar),
                           -std::sin(polar) * std::sin(azimuth)};
            mesh.vertices.push_back({p, p});
        }
    }

    // Each cell between two rings is a quad of two triangles, counter-clockwise
    // seen from outside.
    for (auto stack = 0; stack < stacks; ++stack)
    {
        for (auto slice = 0; slice < slices; ++slice)
        {
            auto top = (std::uint16_t) (stack * (slices + 1) + slice);
            auto bottom = (std::uint16_t) (top + slices + 1);
            const std::uint16_t quad[] = {top,
                                          bottom,
                                          (std::uint16_t) (bottom + 1),
                                          top,
                                          (std::uint16_t) (bottom + 1),
                                          (std::uint16_t) (top + 1)};
            mesh.indices.insert(mesh.indices.end(), quad, quad + 6);
        }
    }

    return mesh;
}

// The asteroid field, generated once. Each rock sits at a random angle around
// the ring, pushed in or out and up or down a little, with a random size,
// orientation and shade of grey-brown. A fixed seed gives the same field on
// every run.
std::vector<Rock> makeRockField()
{
    auto random = std::mt19937 {42};
    auto unit = std::uniform_real_distribution<float> {0.0f, 1.0f};
    auto signedUnit = std::uniform_real_distribution<float> {-1.0f, 1.0f};

    auto rocks = std::vector<Rock> {};
    rocks.reserve(rockCount);

    for (auto i = 0; i < rockCount; ++i)
    {
        auto angle = 2.0f * 3.14159265f * unit(random);

        // Summing two random numbers bunches the rocks towards the middle of
        // the ring and thins them out at its edges.
        auto spread = 0.5f * (signedUnit(random) + signedUnit(random));
        auto radius = ringRadius + spread * ringWidth;
        auto height = 0.12f * signedUnit(random) * (1.0f - std::abs(spread));

        auto position =
            Vec3 {radius * std::cos(angle), height, radius * std::sin(angle)};

        // Mostly small rocks, with the occasional big one.
        auto size = 0.012f + 0.03f * std::pow(unit(random), 3.0f);

        auto model = Mat4::translation(position)
                     * Mat4::rotationY(6.2832f * unit(random))
                     * Mat4::rotationX(6.2832f * unit(random))
                     * Mat4::rotationZ(6.2832f * unit(random)) * Mat4::scale(size);

        auto shade = 0.45f + 0.4f * unit(random);
        auto warmth = 0.1f * unit(random);

        rocks.push_back({model.column(0),
                         model.column(1),
                         model.column(2),
                         model.column(3),
                         {shade + warmth, shade, shade - warmth}});
    }

    return rocks;
}

// The rocks. Position and normal come per vertex from the rock mesh (slot 0);
// the model matrix and tint come per instance from the Rock buffer (slot 1).
// The vertex stage runs once per vertex of every instance, and instanceInput
// hands it the record of the instance it is currently drawing.
struct RockShader final : ShaderProgram
{
    RockShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto model = float4x4(instanceInput(&Rock::model0, 1),
                              instanceInput(&Rock::model1, 1),
                              instanceInput(&Rock::model2, 1),
                              instanceInput(&Rock::model3, 1));
        auto tint = instanceInput(&Rock::tint, 1);

        // The rock's own matrix places it in the ring; the shared ring
        // rotation, one uniform, then turns the whole field at once.
        auto world = ringRotation * model;
        auto worldPosition = world * float4(position, 1.0f);

        // Rotations and uniform scales only, so the model matrix itself can
        // transform the normal (chapter 09's first option).
        auto worldNormal = (world * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * worldPosition);

        auto n = normalize(varying(worldNormal));
        auto diffuse = max(dot(n, sunDirection), 0.0f);
        auto color = varying(tint) * (0.08f + 0.92f * diffuse);
        setFragment(float4(color, 1.0f));
    }

    Uniform<Float4x4> ringRotation;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> sunDirection;

    EACP_SHADER(ringRotation, viewProjection, sunDirection)
};

// The planet: one ordinary draw. Its colour is painted procedurally from the
// local position, swirling bands of sand and rust that turn with the planet.
struct PlanetShader final : ShaderProgram
{
    PlanetShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        setPosition(viewProjection * (model * float4(position, 1.0f)));

        auto p = varying(position);
        auto n = normalize(varying((model * float4(normal, 0.0f)).xyz()));

        // Bands of latitude, bent by a slow wave around the planet.
        auto swirl = sin(p.x() * 2.0f + p.z() * 1.5f) * 0.12f;
        auto bands = sin((p.y() + swirl) * 16.0f) * 0.5f + 0.5f;
        auto fine = sin((p.y() - swirl * 0.5f) * 47.0f) * 0.5f + 0.5f;

        auto sand = float3(constant(0.86f), 0.68f, 0.45f);
        auto rust = float3(constant(0.58f), 0.29f, 0.17f);
        auto surface = mix(rust, sand, bands * 0.75f + fine * 0.25f);

        auto diffuse = max(dot(n, sunDirection), 0.0f);
        setFragment(float4(surface * (0.06f + 0.94f * diffuse), 1.0f));
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> sunDirection;

    EACP_SHADER(model, viewProjection, sunDirection)
};
} // namespace

struct InstancingView final : GPUView
{
    InstancingView()
    {
        setDepth(true);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = true;
        pipeline.cullMode = CullMode::Back;

        // The rock mesh goes in once as ordinary vertices, and the 12,000
        // records go in once as per-instance data in slot 1. Neither changes
        // again: animating the ring is a single uniform.
        rockShader.setVertices(rockMesh.data(), (int) rockMesh.size());
        rockShader.setInstances(1, rocks.data(), (int) rocks.size());
        rockShader.prepare(pipeline);

        planetShader.setVertices(sphere.vertices.data(),
                                 (int) sphere.vertices.size());
        planetShader.setIndices(sphere.indices.data(), (int) sphere.indices.size());
        planetShader.prepare(pipeline);

        setContinuous(true);

        std::printf("Instancing: %d rocks of %d vertices each. Press Space to "
                    "switch between one draw call and one per rock.\n",
                    rockCount,
                    (int) rockMesh.size());
        std::fflush(stdout);
    }

    void keyDown(const Graphics::KeyEvent& event) override
    {
        if (event.keyCode != Graphics::KeyCode::Space || event.modifiers.command)
        {
            passKeyOn();
            return;
        }

        instanced = !instanced;
        resetTimings();
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        ringAngle += 0.04f * delta;
        planetAngle += 0.1f * delta;

        frameSeconds += delta;
        ++frames;

        // Every two seconds, report the average time between frames and the
        // average CPU time render() spent recording the draws.
        if (frameSeconds >= 2.0f)
        {
            std::printf("%-12s %5d draw calls   frame %6.2f ms   render() CPU "
                        "%6.2f ms\n",
                        instanced ? "instanced" : "one by one",
                        instanced ? 2 : rockCount + 1,
                        1000.0f * frameSeconds / (float) frames,
                        1000.0f * renderSeconds / (float) frames);
            std::fflush(stdout);
            resetTimings();
        }
    }

    void render(Frame& frame) override
    {
        auto start = std::chrono::steady_clock::now();

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt({0.0f, 2.2f, 7.6f}, {0.0f, -0.3f, 0.0f}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;
        auto sunDirection = normalize(Vec3 {-1.0f, 0.35f, 0.45f});

        planetShader.model =
            Mat4::rotationZ(0.2f) * Mat4::rotationY(planetAngle) * Mat4::scale(1.4f);
        planetShader.viewProjection = viewProjection;
        planetShader.sunDirection = sunDirection;

        rockShader.ringRotation = Mat4::rotationY(ringAngle);
        rockShader.viewProjection = viewProjection;
        rockShader.sunDirection = sunDirection;

        auto pass = frame.beginPass({Graphics::Color {0.01f, 0.01f, 0.025f}});
        pass.draw(planetShader);

        if (instanced)
        {
            // One draw call: the GPU runs the rock mesh rockCount times, with
            // the instance index stepping through the Rock buffer.
            pass.drawInstanced(rockShader, rockCount);
        }
        else
        {
            // The way chapters 11, 13 and 14 drew their objects: one draw call per
            // rock. Each call draws a single instance, starting at rock i, so
            // the picture is identical; only the number of calls changes.
            for (auto i = 0; i < rockCount; ++i)
                pass.drawInstanced(rockShader, 1, i);
        }

        renderSeconds +=
            std::chrono::duration<float>(std::chrono::steady_clock::now() - start)
                .count();
    }

    void resetTimings()
    {
        frameSeconds = 0.0f;
        renderSeconds = 0.0f;
        frames = 0;
    }

    std::vector<Vertex> rockMesh = makeRock();
    std::vector<Rock> rocks = makeRockField();
    SphereMesh sphere = makeSphere(48, 96);

    RockShader rockShader;
    PlanetShader planetShader;

    bool instanced = true;
    float ringAngle = 0.0f;
    float planetAngle = 0.0f;

    float frameSeconds = 0.0f;
    float renderSeconds = 0.0f;
    int frames = 0;
};

int main()
{
    return Graphics::runWindowedApp<InstancingView>();
}
