// 17. Cubemaps: a sky painted procedurally into the six faces of a cube map,
// drawn as a skybox around the scene, and reflected and refracted by the two
// objects in it: a mirror cube and a glass sphere. Drag to orbit the camera,
// scroll to move it nearer or further.
#include <eacp/GPU/GPU.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace eacp;
using namespace GPU;
using namespace Maths;

struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

// The skybox only needs a position: it is also the direction the cube map is
// sampled with.
struct SkyVertex
{
    Vec3 position;
};

namespace
{
// ---------------------------------------------------------------------------
// The sky, as a function of direction
// ---------------------------------------------------------------------------

// The direction the sun is in. Everything about the sky below is a function of
// a unit direction only: a cube map stores exactly that, so it has no idea how
// far away anything is.
const Vec3 sunDirection = normalize(Vec3 {-0.35f, 0.32f, -0.88f});

float smoothStep(float edge0, float edge1, float x)
{
    auto t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 mixColor(Vec3 a, Vec3 b, float t)
{
    return a + (b - a) * t;
}

// A smooth pseudo-random value in 0..1 for any 2D point: a random number at
// each integer grid point, blended between them. Summing it at several scales
// ("octaves") gives the soft, lumpy shapes clouds have.
float hash(int x, int y)
{
    auto n = static_cast<std::uint32_t>(x) * 374761393u
             + static_cast<std::uint32_t>(y) * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return static_cast<float>((n ^ (n >> 16)) & 0xffffu) / 65535.0f;
}

float valueNoise(float x, float y)
{
    auto x0 = static_cast<int>(std::floor(x));
    auto y0 = static_cast<int>(std::floor(y));
    auto fx = smoothStep(0.0f, 1.0f, x - static_cast<float>(x0));
    auto fy = smoothStep(0.0f, 1.0f, y - static_cast<float>(y0));

    auto top = hash(x0, y0) + (hash(x0 + 1, y0) - hash(x0, y0)) * fx;
    auto bottom = hash(x0, y0 + 1) + (hash(x0 + 1, y0 + 1) - hash(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

float clouds(float x, float y)
{
    auto sum = 0.0f;
    auto amplitude = 0.5f;

    for (auto octave = 0; octave < 5; ++octave)
    {
        sum += amplitude * valueNoise(x, y);
        x *= 2.03f;
        y *= 2.03f;
        amplitude *= 0.5f;
    }

    return sum;
}

// The colour of the world seen in direction d (unit length). Above the horizon:
// a blue gradient, clouds, a ring of mountains and the sun. Below it: a
// chequered ground plane that fades into haze in the distance.
Vec3 skyColor(Vec3 d)
{
    const auto zenith = Vec3 {0.16f, 0.38f, 0.82f};
    const auto horizon = Vec3 {0.80f, 0.87f, 0.95f};

    auto color = Vec3 {};

    if (d.y >= 0.0f)
    {
        color = mixColor(horizon, zenith, std::sqrt(d.y));

        // Clouds: noise painted on a flat ceiling one unit above the eye. The
        // point where the ray d meets that ceiling is d.xz / d.y.
        if (d.y > 0.01f)
        {
            auto cx = d.x / d.y;
            auto cz = d.z / d.y;
            auto density = smoothStep(0.52f, 0.78f, clouds(cx * 0.9f, cz * 0.9f));
            density *= smoothStep(0.03f, 0.25f, d.y);
            color = mixColor(color, Vec3 {0.97f, 0.97f, 1.0f}, 0.9f * density);
        }

        // Mountains: a skyline whose height depends on the compass direction.
        auto azimuth = std::atan2(d.z, d.x);
        auto skyline = 0.07f + 0.04f * std::sin(3.0f * azimuth + 1.0f)
                       + 0.025f * std::sin(7.0f * azimuth + 2.0f)
                       + 0.012f * std::sin(17.0f * azimuth);
        auto elevation = std::asin(std::clamp(d.y, 0.0f, 1.0f));

        // A soft edge a texel or so wide, so the ridge stays smooth when the
        // texture is magnified on screen.
        auto mountain = 1.0f - smoothStep(skyline - 0.004f, skyline, elevation);

        if (mountain > 0.0f)
        {
            auto shade = std::min(elevation / skyline, 1.0f);
            auto rock = mixColor(
                Vec3 {0.42f, 0.47f, 0.58f}, Vec3 {0.30f, 0.33f, 0.45f}, shade);
            // Snow on whatever rises above the snow line.
            auto snow = smoothStep(0.095f, 0.10f, elevation);
            rock = mixColor(rock, Vec3 {0.95f, 0.96f, 1.0f}, snow);
            color = mixColor(color, rock, mountain);
        }

        // The sun: a soft glow, and a white disc about two degrees across.
        auto towardsSun = std::max(dot(d, sunDirection), 0.0f);
        auto glow =
            0.25f * std::pow(towardsSun, 64.0f) + 0.12f * std::pow(towardsSun, 6.0f);
        color = color + Vec3 {1.0f, 0.85f, 0.6f} * glow;

        auto disc = smoothStep(0.99935f, 0.99950f, towardsSun);
        color = mixColor(color, Vec3 {1.0f, 1.0f, 1.0f}, disc);
    }
    else
    {
        // The ground: a chequerboard on a floor one unit below the eye, where
        // the ray meets it at d.xz / -d.y, with four tiles to the unit.
        auto gx = 4.0f * d.x / -d.y;
        auto gz = 4.0f * d.z / -d.y;
        auto tile =
            (static_cast<int>(std::floor(gx)) + static_cast<int>(std::floor(gz)))
            & 1;
        color = tile ? Vec3 {0.36f, 0.58f, 0.28f} : Vec3 {0.25f, 0.45f, 0.22f};

        // Dark grid lines between the tiles.
        auto lineX = std::abs(gx - std::round(gx));
        auto lineZ = std::abs(gz - std::round(gz));
        if (std::min(lineX, lineZ) < 0.04f)
            color = Vec3 {0.12f, 0.20f, 0.10f};

        // Distant ground fades into the haze at the horizon.
        auto haze = 1.0f - smoothStep(0.0f, 0.3f, -d.y);
        color = mixColor(color, horizon * 0.9f, haze);
    }

    return color;
}

// ---------------------------------------------------------------------------
// Baking the sky into a cube map
// ---------------------------------------------------------------------------

constexpr int faceSize = 512;

// The direction through texel (u, v) of one face, for u and v in 0..1 with
// (0, 0) the face's top-left texel (row 0). This is the inverse of the lookup
// the GPU does: given a direction, take its largest component to pick the face,
// divide the other two by it to get s and t in -1..1, and use those. The face
// order is +X, -X, +Y, -Y, +Z, -Z.
Vec3 faceDirection(int face, float u, float v)
{
    auto s = 2.0f * u - 1.0f;
    auto t = 2.0f * v - 1.0f;

    switch (face)
    {
        case 0:
            return {1.0f, -t, -s}; // +X
        case 1:
            return {-1.0f, -t, s}; // -X
        case 2:
            return {s, 1.0f, t}; // +Y
        case 3:
            return {s, -1.0f, -t}; // -Y
        case 4:
            return {s, -t, 1.0f}; // +Z
        default:
            return {-s, -t, -1.0f}; // -Z
    }
}

std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Six square faces, one after another in +X, -X, +Y, -Y, +Z, -Z order, each
// stored row by row from the top: one block of pixels, uploaded in one call.
Texture makeSkyCubeMap()
{
    auto pixels = std::vector<std::uint8_t>(6 * faceSize * faceSize * 4);
    auto* texel = pixels.data();

    for (auto face = 0; face < 6; ++face)
        for (auto y = 0; y < faceSize; ++y)
            for (auto x = 0; x < faceSize; ++x)
            {
                // Four samples spread over the texel, averaged, so the
                // sharp edges (grid lines, mountains, the sun) are smooth.
                auto color = Vec3 {};

                for (auto sample = 0; sample < 4; ++sample)
                {
                    auto u = (x + 0.25f + 0.5f * (sample % 2)) / faceSize;
                    auto v = (y + 0.25f + 0.5f * (sample / 2)) / faceSize;
                    color = color + skyColor(normalize(faceDirection(face, u, v)));
                }

                color = color * 0.25f;

                *texel++ = toByte(color.x);
                *texel++ = toByte(color.y);
                *texel++ = toByte(color.z);
                *texel++ = 255;
            }

    auto descriptor = TextureDescriptor {};
    descriptor.width = faceSize;
    descriptor.height = faceSize;
    descriptor.format = TextureFormat::RGBA8Unorm;
    descriptor.cube = true;
    // Curved mirrors squeeze a lot of sky into a few pixels; mipmaps keep that
    // from shimmering.
    descriptor.mipmapped = true;

    return Device::shared().makeTexture(descriptor, pixels.data());
}

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

// The unit cube from chapter 09, with flat per-face normals.
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

    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const std::uint16_t quad[] = {0, 1, 2, 0, 2, 3};

    auto mesh = Mesh {};

    for (const auto& face: faces)
    {
        auto first = static_cast<std::uint16_t>(mesh.vertices.size());

        for (const auto& corner: corners)
        {
            auto position =
                (face.normal + face.across1 * corner[0] + face.across2 * corner[1])
                * 0.5f;
            mesh.vertices.push_back({position, face.normal});
        }

        for (auto index: quad)
            mesh.indices.push_back(static_cast<std::uint16_t>(first + index));
    }

    return mesh;
}

// A sphere of radius 1 made of rings of latitude and segments of longitude. On
// a sphere centred at the origin the normal is the position itself, and the
// vertices are shared between neighbouring quads, so the normals blend smoothly.
Mesh makeSphere(int rings, int segments)
{
    auto mesh = Mesh {};

    for (auto ring = 0; ring <= rings; ++ring)
    {
        auto polar = pi * static_cast<float>(ring) / static_cast<float>(rings);

        for (auto segment = 0; segment <= segments; ++segment)
        {
            auto azimuth = 2.0f * pi * static_cast<float>(segment)
                           / static_cast<float>(segments);
            auto normal = Vec3 {std::sin(polar) * std::cos(azimuth),
                                std::cos(polar),
                                std::sin(polar) * std::sin(azimuth)};
            mesh.vertices.push_back({normal, normal});
        }
    }

    // Two triangles per quad, counter-clockwise seen from outside.
    for (auto ring = 0; ring < rings; ++ring)
        for (auto segment = 0; segment < segments; ++segment)
        {
            auto a = static_cast<std::uint16_t>(ring * (segments + 1) + segment);
            auto b = static_cast<std::uint16_t>(a + segments + 1);
            auto c = static_cast<std::uint16_t>(b + 1);
            auto d = static_cast<std::uint16_t>(a + 1);

            for (auto index: {a, d, c, a, c, b})
                mesh.indices.push_back(index);
        }

    return mesh;
}

// The skybox: the eight corners of a cube around the origin, with its faces
// wound counter-clockwise seen from *outside*, like every other closed mesh.
// The camera sits inside it, so it sees every face from behind.
constexpr SkyVertex skyCorners[8] = {
    {{-1, -1, -1}},
    {{1, -1, -1}},
    {{1, 1, -1}},
    {{-1, 1, -1}},
    {{-1, -1, 1}},
    {{1, -1, 1}},
    {{1, 1, 1}},
    {{-1, 1, 1}},
};

constexpr std::uint16_t skyIndices[36] = {
    4, 5, 6, 4, 6, 7, // +Z
    1, 0, 3, 1, 3, 2, // -Z
    5, 1, 2, 5, 2, 6, // +X
    0, 4, 7, 0, 7, 3, // -X
    3, 7, 6, 3, 6, 2, // +Y
    0, 1, 5, 0, 5, 4, // -Y
};

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// The sky. The view matrix it gets has had its translation removed, so the box
// turns with the camera but never moves relative to it: however far you walk,
// the sky stays infinitely far away.
struct SkyboxShader final : ShaderProgram
{
    SkyboxShader()
    {
        // Sampler state is fixed at compile(). Clamp is the only sensible
        // address mode on a cube map: the hardware handles the seams between
        // faces itself.
        sky.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&SkyVertex::position);
        auto clip = viewProjection * float4(position, 1.0f);

        // Replace z with w, so that after the divide by w the depth is exactly
        // 1.0: the far plane. Every pixel of the sky is behind everything else.
        setPosition(clip.xyww());

        // The corner of the box is also the direction from the camera to it,
        // and a cube map is sampled by direction. The length does not matter.
        setFragment(sample(sky, varying(position)));
    }

    Uniform<Float4x4> viewProjection;
    Uniform<TextureCube> sky;

    EACP_SHADER(viewProjection, sky)
};

// The mirror: the direction from the eye to the surface, reflected about the
// normal, looks up what the surface sees.
struct ReflectShader final : ShaderProgram
{
    ReflectShader()
    {
        sky.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();
        setPosition(viewProjection * float4(worldPosition, 1.0f));

        // incident points from the eye to the surface: the direction the view
        // ray travels. reflect() mirrors it about the normal.
        auto incident = normalize(varying(worldPosition) - cameraPosition);
        auto n = normalize(varying(worldNormal));
        auto reflected = reflect(incident, n);

        setFragment(sample(sky, reflected));
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> cameraPosition;
    Uniform<TextureCube> sky;

    EACP_SHADER(model, normalMatrix, viewProjection, cameraPosition, sky)
};

// The glass: the same view ray, bent at the surface by refract() instead of
// mirrored. ratio is the refractive index of the medium the ray leaves divided
// by the one it enters: air (1.00) into glass (1.52).
struct RefractShader final : ShaderProgram
{
    RefractShader()
    {
        sky.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();
        setPosition(viewProjection * float4(worldPosition, 1.0f));

        auto incident = normalize(varying(worldPosition) - cameraPosition);
        auto n = normalize(varying(worldNormal));
        auto refracted = refract(incident, n, ratio);

        // A faint green-blue tint, as thick glass has.
        auto seen = sample(sky, refracted).xyz() * tint;
        setFragment(float4(seen, 1.0f));
    }

    static constexpr float ratio = 1.00f / 1.52f;

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> tint;
    Uniform<TextureCube> sky;

    EACP_SHADER(model, normalMatrix, viewProjection, cameraPosition, tint, sky)
};
} // namespace

struct CubemapsView final : GPUView
{
    CubemapsView()
    {
        setDepth(true);
        setHandlesMouseEvents(true);

        // The two objects: depth-tested and back-face culled, as in chapter 08.
        auto objects = RenderPipelineDescriptor {};
        objects.sampleCount = sampleCount();
        objects.depth = true;
        objects.cullMode = CullMode::Back;

        mirror.setVertices(cube.vertices.data(), (int) cube.vertices.size());
        mirror.setIndices(cube.indices.data(), (int) cube.indices.size());
        mirror.sky = skyTexture;
        mirror.prepare(objects);

        glass.setVertices(sphere.vertices.data(), (int) sphere.vertices.size());
        glass.setIndices(sphere.indices.data(), (int) sphere.indices.size());
        glass.sky = skyTexture;
        glass.prepare(objects);

        // The sky is drawn last. Its depth is exactly 1.0, the value the depth
        // buffer was cleared to, so it needs LessEqual (not Less) to pass where
        // nothing has been drawn, and it fails wherever an object is in front.
        // It never needs to write depth: nothing is drawn after it. We see its
        // faces from inside, so from behind: its front faces are clockwise.
        auto skyPipeline = RenderPipelineDescriptor {};
        skyPipeline.sampleCount = sampleCount();
        skyPipeline.depth = true;
        skyPipeline.depthCompare = DepthCompare::LessEqual;
        skyPipeline.depthWrite = false;
        skyPipeline.cullMode = CullMode::Back;
        skyPipeline.frontFace = Winding::Clockwise;

        skybox.setVertices(skyCorners);
        skybox.setIndices(skyIndices);
        skybox.sky = skyTexture;
        skybox.prepare(skyPipeline);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        cubeAngle += 0.3f * delta;

        // The camera drifts around the scene on its own until you grab it.
        if (!dragging)
            yaw += 0.12f * delta;
    }

    void mouseDown(const Graphics::MouseEvent&) override { dragging = true; }
    void mouseUp(const Graphics::MouseEvent&) override { dragging = false; }

    void mouseDragged(const Graphics::MouseEvent& event) override
    {
        yaw -= 0.01f * event.delta.x;
        pitch = std::clamp(pitch + 0.01f * event.delta.y, -1.4f, 1.4f);
    }

    void mouseWheel(const Graphics::MouseEvent& event) override
    {
        distance = std::clamp(distance - 0.05f * event.delta.y, 2.5f, 12.0f);
    }

    void render(Frame& frame) override
    {
        // An orbit: yaw around the y axis, pitch above the ground, distance out.
        auto cameraPosition = Vec3 {distance * std::cos(pitch) * std::sin(yaw),
                                    distance * std::sin(pitch),
                                    distance * std::cos(pitch) * std::cos(yaw)};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 0, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(50.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        // The view matrix with its translation (the fourth column) taken out:
        // only the camera's rotation is left.
        auto skyView = view;
        skyView.setColumn(3, {0.0f, 0.0f, 0.0f, 1.0f});
        skybox.viewProjection = projection * skyView;

        auto cubeModel = Mat4::translation({-1.2f, 0.0f, 0.0f})
                         * Mat4::rotationY(cubeAngle) * Mat4::rotationX(-0.15f)
                         * Mat4::scale(1.3f);
        mirror.model = cubeModel;
        mirror.normalMatrix = cubeModel.inverted().transposed();
        mirror.viewProjection = viewProjection;
        mirror.cameraPosition = cameraPosition;

        auto sphereModel =
            Mat4::translation({1.2f, 0.0f, 0.0f}) * Mat4::scale(0.85f);
        glass.model = sphereModel;
        glass.normalMatrix = sphereModel.inverted().transposed();
        glass.viewProjection = viewProjection;
        glass.cameraPosition = cameraPosition;
        glass.tint = Vec3 {0.92f, 0.98f, 0.96f};

        // The sky covers every pixel the objects leave, so the clear colour is
        // never seen.
        auto pass = frame.beginPass({Graphics::Color {0.0f, 0.0f, 0.0f}});
        pass.draw(mirror);
        pass.draw(glass);
        pass.draw(skybox);
    }

    // The texture is declared before the shaders, which hold a pointer to it.
    Texture skyTexture = makeSkyCubeMap();
    Mesh cube = makeCube();
    Mesh sphere = makeSphere(48, 96);

    ReflectShader mirror;
    RefractShader glass;
    SkyboxShader skybox;

    float cubeAngle = 0.0f;
    float yaw = 0.6f;
    float pitch = 0.1f;
    float distance = 5.0f;
    bool dragging = false;
};

int main()
{
    return Graphics::runWindowedApp<CubemapsView>();
}
