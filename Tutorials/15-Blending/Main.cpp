// 15. Blending: a small scene with two kinds of see-through. Tufts of grass are
// drawn from a texture whose background is fully transparent, and those texels
// are thrown away with discard. Coloured glass windows are partly transparent,
// so they are blended over what is behind them, which means they must be drawn
// after everything opaque and sorted back to front as the camera orbits.
#include <eacp/GPU/GPU.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace eacp;
using namespace GPU;
using namespace Maths;

// Flip these to see the two problems this chapter is about. Unsorted, a window
// drawn before one behind it is blended over a background that does not have
// the far window in it yet. Writing depth as well, the far window is then not
// merely missing from the blend but hidden: the depth test rejects it.
constexpr bool sortWindows = true;
constexpr bool windowsWriteDepth = false;

struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

namespace
{
struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

// Appends one quad from its four corners, listed counter-clockwise as seen from
// the side it faces, with UV (0, 0) on the top-left corner.
void addQuad(Mesh& mesh, const std::array<Vec3, 4>& corners, Vec3 normal, Vec2 uvMax)
{
    auto first = static_cast<std::uint16_t>(mesh.vertices.size());
    const Vec2 uvs[] = {{0, uvMax.y}, {uvMax.x, uvMax.y}, {uvMax.x, 0}, {0, 0}};

    for (auto c = 0; c < 4; ++c)
        mesh.vertices.push_back({corners[c], normal, uvs[c]});

    for (auto i: {0, 1, 2, 0, 2, 3})
        mesh.indices.push_back(static_cast<std::uint16_t>(first + i));
}

// The unit cube from chapter 09, now with a UV on each corner so every face
// shows the whole texture.
Mesh makeCube()
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

    auto mesh = Mesh {};

    for (const auto& f: faces)
    {
        auto corner = [&](float a, float b)
        { return (f.normal + f.across1 * a + f.across2 * b) * 0.5f; };

        addQuad(mesh,
                {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)},
                f.normal,
                {1, 1});
    }

    return mesh;
}

// A 10 x 10 floor at y = 0, facing up. Its UVs run to 5 so a repeating sampler
// tiles the texture five times across it.
Mesh makeFloor()
{
    auto mesh = Mesh {};
    addQuad(mesh,
            {Vec3 {-5, 0, 5}, Vec3 {5, 0, 5}, Vec3 {5, 0, -5}, Vec3 {-5, 0, -5}},
            {0, 1, 0},
            {5, 5});
    return mesh;
}

// One upright quad, 1 x 1, standing on y = 0 and facing +z. Grass and windows
// are both this quad, placed by their model matrices.
Mesh makeUprightQuad()
{
    auto mesh = Mesh {};
    addQuad(mesh,
            {Vec3 {-0.5f, 0, 0},
             Vec3 {0.5f, 0, 0},
             Vec3 {0.5f, 1, 0},
             Vec3 {-0.5f, 1, 0}},
            {0, 0, 1},
            {1, 1});
    return mesh;
}

// Every texture in this chapter is made the same way: a function decides the
// RGBA of each texel (all four in 0..1) and the result is uploaded as RGBA8.
template <typename TexelFunction>
Texture makeTexture(int size, TexelFunction texelAt)
{
    auto pixels = std::vector<std::uint8_t>(size * size * 4);

    for (auto y = 0; y < size; ++y)
    {
        for (auto x = 0; x < size; ++x)
        {
            auto u = (x + 0.5f) / size;
            auto v = (y + 0.5f) / size;
            auto rgba = texelAt(u, v);
            auto* texel = &pixels[(y * size + x) * 4];

            for (auto c = 0; c < 4; ++c)
                texel[c] = static_cast<std::uint8_t>(
                    255.0f * std::clamp(rgba[c], 0.0f, 1.0f) + 0.5f);
        }
    }

    auto descriptor = TextureDescriptor {};
    descriptor.width = size;
    descriptor.height = size;
    descriptor.format = TextureFormat::RGBA8Unorm;
    return Device::shared().makeTexture(descriptor, pixels.data());
}

// A two-tone checkerboard for the floor. Fully opaque.
Texture makeFloorTexture()
{
    return makeTexture(32,
                       [](float u, float v)
                       {
                           auto light = (int(u * 2) + int(v * 2)) % 2 == 0;
                           auto shade = light ? 0.55f : 0.40f;
                           return Vec4 {shade, shade * 0.95f, shade * 0.9f, 1};
                       });
}

// A wooden crate: planks with a dark frame round the edge. Fully opaque.
Texture makeCrateTexture()
{
    return makeTexture(64,
                       [](float u, float v)
                       {
                           auto edge = std::min({u, v, 1 - u, 1 - v});
                           auto plank = std::fmod(v * 4.0f, 1.0f);
                           auto grain = 0.9f + 0.1f * std::sin(u * 40.0f + v * 7.0f);
                           auto shade = edge < 0.1f                      ? 0.45f
                                        : plank < 0.06f || plank > 0.94f ? 0.6f
                                                                         : grain;
                           return Vec4 {
                               0.75f * shade, 0.52f * shade, 0.3f * shade, 1};
                       });
}

// A tuft of grass: a handful of tapering, leaning blades. A texel inside a
// blade has alpha 1, every other texel alpha 0. There is no in-between, which
// is what makes discarding (rather than blending) the right tool for it.
Texture makeGrassTexture()
{
    struct Blade
    {
        float base, lean, width, height;
    };

    const Blade blades[] = {
        {0.18f, -0.10f, 0.07f, 0.70f},
        {0.30f, 0.05f, 0.08f, 0.95f},
        {0.42f, -0.18f, 0.06f, 0.80f},
        {0.50f, 0.12f, 0.09f, 0.98f},
        {0.60f, -0.04f, 0.07f, 0.85f},
        {0.70f, 0.20f, 0.08f, 0.75f},
        {0.82f, 0.06f, 0.06f, 0.65f},
    };

    return makeTexture(
        128,
        [&](float u, float v)
        {
            // Height above the bottom of the image, 0..1.
            auto h = 1.0f - v;

            for (const auto& b: blades)
            {
                if (h > b.height)
                    continue;

                // The blade's centre leans as it rises, and its
                // half-width narrows to nothing at the tip.
                auto t = h / b.height;
                auto centre = b.base + b.lean * t * t;
                auto halfWidth = 0.5f * b.width * (1.0f - t);

                if (std::abs(u - centre) < halfWidth)
                    return Vec4 {0.20f + 0.25f * t, 0.45f + 0.35f * t, 0.12f, 1};
            }

            return Vec4 {0, 0, 0, 0};
        });
}

// A pane of glass in a frame. The frame and the cross bars are opaque, the
// glass is mostly see-through, with a brighter diagonal streak so it reads as
// glass. The colour is near white: each window tints it its own colour.
Texture makeWindowTexture()
{
    return makeTexture(64,
                       [](float u, float v)
                       {
                           auto edge = std::min({u, v, 1 - u, 1 - v});
                           auto bar =
                               std::min(std::abs(u - 0.5f), std::abs(v - 0.5f));

                           if (edge < 0.07f || bar < 0.025f)
                               return Vec4 {0.85f, 0.85f, 0.85f, 1};

                           auto streak =
                               std::abs(u + v - 0.8f) < 0.08f ? 0.25f : 0.0f;
                           return Vec4 {1, 1, 1, 0.35f + streak};
                       });
}

// One program for everything in the scene: a textured surface, tinted, with a
// little fixed directional light so the cube faces read. The two flags are
// baked in when the program is compiled, so each kind of object gets its own
// instance.
struct SceneShader final : ShaderProgram
{
    SceneShader(TextureSampling sampling, bool alphaTest)
        : alphaTested(alphaTest)
    {
        image.sampling = sampling;
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);
        auto uv = varying(vertexInput(&Vertex::uv));

        auto worldNormal = (model * float4(normal, 0.0f)).xyz();
        setPosition(viewProjection * model * float4(position, 1.0f));

        auto n = normalize(varying(worldNormal));
        auto texel = sample(image, uv);

        // abs() so that a quad seen from behind is lit the same as from in
        // front: grass and glass have no "wrong" side.
        auto shade = 0.55f + 0.45f * abs(dot(n, lightDirection));
        auto color = texel.xyz() * tint.xyz() * shade;
        auto alpha = texel.w() * tint.w();

        // The alpha test: a fragment whose alpha is under one half never
        // reaches the colour or depth buffer at all.
        if (alphaTested)
            setDiscardBelow(alpha, 0.5f);

        setFragment(float4(color, alpha));
    }

    bool alphaTested;

    Uniform<Float4x4> model;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> lightDirection;
    Uniform<Float4> tint;
    Uniform<Texture2D> image;

    EACP_SHADER(model, viewProjection, lightDirection, tint, image)
};

// The classic "over" blend, written out as the equation rather than taken
// from BlendMode::AlphaBlend (which is the same thing):
//
//     result = source * source.a + destination * (1 - source.a)
BlendState overBlend()
{
    auto blend = BlendState {};
    blend.enabled = true;
    blend.sourceColor = BlendFactor::SourceAlpha;
    blend.destinationColor = BlendFactor::OneMinusSourceAlpha;
    blend.colorOperation = BlendOperation::Add;
    blend.sourceAlpha = BlendFactor::One;
    blend.destinationAlpha = BlendFactor::OneMinusSourceAlpha;
    blend.alphaOperation = BlendOperation::Add;
    return blend;
}

struct Window
{
    Vec3 position; // the middle of its bottom edge
    float yaw;
    Vec4 tint;
};

// Listed nearest first as seen from where the camera starts, the worst order
// to draw them in: turn sortWindows off to see what that does.
const Window windows[] = {
    {{1.0f, 0.1f, 1.9f}, 0.60f, {0.8f, 0.4f, 1.0f, 1}},
    {{-0.4f, 0.1f, 1.4f}, 0.25f, {1.0f, 0.35f, 0.35f, 1}},
    {{0.5f, 0.1f, 0.5f}, -0.35f, {0.35f, 1.0f, 0.45f, 1}},
    {{-0.6f, 0.1f, -0.4f}, 0.40f, {0.35f, 0.55f, 1.0f, 1}},
    {{0.3f, 0.1f, -1.4f}, -0.10f, {1.0f, 0.8f, 0.3f, 1}},
};

const Vec3 cratePositions[] = {{-1.9f, 0.5f, -1.1f}, {1.9f, 0.5f, 0.8f}};

const Vec3 grassPositions[] = {
    {-1.9f, 0, 0.5f},
    {1.8f, 0, -0.9f},
    {-1.1f, 0, -2.2f},
    {1.5f, 0, 2.2f},
    {-1.2f, 0, 2.4f},
    {2.6f, 0, 0.0f},
};
} // namespace

struct BlendingView final : GPUView
{
    BlendingView()
    {
        setDepth(true);

        auto opaque = RenderPipelineDescriptor {};
        opaque.sampleCount = sampleCount();
        opaque.depth = true;
        opaque.cullMode = CullMode::Back;

        // Grass is a flat quad seen from both sides, so nothing is culled.
        // Otherwise it is opaque: discarded texels are simply not there.
        auto foliage = opaque;
        foliage.cullMode = CullMode::None;

        // Glass is tested against the depth the opaque objects wrote, so a
        // crate in front still hides it, and it blends instead of replacing.
        auto glass = foliage;
        glass.depthWrite = windowsWriteDepth;
        glass.blend = overBlend();

        prepare(floorShader, floorMesh, opaque, floorTexture);
        prepare(crateShader, cubeMesh, opaque, crateTexture);
        prepare(grassShader, quadMesh, foliage, grassTexture);
        prepare(windowShader, quadMesh, glass, windowTexture);

        setContinuous(true);
    }

    static void prepare(SceneShader& shader,
                        const Mesh& mesh,
                        const RenderPipelineDescriptor& pipeline,
                        const Texture& texture)
    {
        shader.setVertices(mesh.vertices.data(), (int) mesh.vertices.size());
        shader.setIndices(mesh.indices.data(), (int) mesh.indices.size());
        shader.prepare(pipeline);
        shader.image = texture;
        shader.tint = Vec4 {1, 1, 1, 1};
        shader.model = Mat4 {};
    }

    void update(Threads::FrameTime time) override
    {
        cameraAngle += 0.15f * static_cast<float>(time.delta);
    }

    void render(Frame& frame) override
    {
        // The camera circles the scene, a little above it.
        auto cameraPosition =
            Vec3 {5.5f * std::sin(cameraAngle), 2.2f, 5.5f * std::cos(cameraAngle)};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 0.4f, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;
        auto lightDirection = normalize(Vec3 {0.4f, 1.0f, 0.3f});

        for (auto* shader: {&floorShader, &crateShader, &grassShader, &windowShader})
        {
            shader->viewProjection = viewProjection;
            shader->lightDirection = lightDirection;
        }

        auto pass = frame.beginPass({Graphics::Color {0.55f, 0.70f, 0.85f}});

        // 1. Everything opaque, in any order: the depth buffer sorts it.
        pass.draw(floorShader);

        for (auto position: cratePositions)
        {
            crateShader.model = Mat4::translation(position);
            pass.draw(crateShader);
        }

        // Grass counts as opaque too. Each tuft is two quads crossed at right
        // angles, so it never vanishes edge-on as the camera goes round.
        for (auto i = 0; i < (int) std::size(grassPositions); ++i)
        {
            auto base = Mat4::translation(grassPositions[i])
                        * Mat4::rotationY(0.7f * (float) i) * Mat4::scale(0.8f);

            for (auto turn: {0.0f, halfPi})
            {
                grassShader.model = base * Mat4::rotationY(turn);
                pass.draw(grassShader);
            }
        }

        // 2. The transparent windows last, the furthest from the camera first,
        // so each one blends over a picture that already holds everything
        // behind it. The camera moves, so the order is worked out every frame.
        auto order = std::array<int, std::size(windows)> {};

        for (auto i = 0; i < (int) order.size(); ++i)
            order[i] = i;

        if (sortWindows)
        {
            auto distanceTo = [&](int i)
            {
                auto centre = windows[i].position + Vec3 {0, 0.5f, 0};
                return lengthSquared(centre - cameraPosition);
            };

            std::sort(order.begin(),
                      order.end(),
                      [&](int a, int b) { return distanceTo(a) > distanceTo(b); });
        }

        for (auto i: order)
        {
            windowShader.model = Mat4::translation(windows[i].position)
                                 * Mat4::rotationY(windows[i].yaw);
            windowShader.tint = windows[i].tint;
            pass.draw(windowShader);
        }
    }

    // The shaders only point at their textures, so the textures are declared
    // first and outlive them.
    Texture floorTexture = makeFloorTexture();
    Texture crateTexture = makeCrateTexture();
    Texture grassTexture = makeGrassTexture();
    Texture windowTexture = makeWindowTexture();

    Mesh floorMesh = makeFloor();
    Mesh cubeMesh = makeCube();
    Mesh quadMesh = makeUprightQuad();

    SceneShader floorShader {{TextureFilter::Linear, TextureAddressMode::Repeat},
                             false};
    SceneShader crateShader {{TextureFilter::Linear, TextureAddressMode::Clamp},
                             false};
    // Clamp matters here: with Repeat, linear filtering along the top edge
    // would blend in the opaque bottom row and draw a thin line over the tips.
    SceneShader grassShader {{TextureFilter::Linear, TextureAddressMode::Clamp},
                             true};
    SceneShader windowShader {{TextureFilter::Linear, TextureAddressMode::Clamp},
                              false};

    float cameraAngle = 0.0f;
};

int main()
{
    return Graphics::runWindowedApp<BlendingView>();
}
