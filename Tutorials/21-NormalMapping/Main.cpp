// 21. Normal Mapping: a brick wall lit by an orbiting point light. A normal map
// stores a surface direction for every texel, so the flat wall's lighting
// follows the bricks' bevelled edges and the mortar grooves between them. The
// left half of the window shades the wall with its single flat normal, the
// right half with the normal map; Space cycles between the split, all flat and
// all normal-mapped.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace NormalMapping
{
// Chapter 12's vertex, plus the two directions a normal map needs: which way
// the texture's u and v axes run across the surface at this vertex.
struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec3 tangent; // the direction of increasing u (to the right in the texture)
    Vec3 bitangent; // the direction of increasing v (down the texture)
};

namespace
{
struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

// One square face, as in chapter 09's cube: an outward normal and two axes
// across it with across1 x across2 = normal, so the corners come out
// counter-clockwise from outside. The face sits `offset` from the origin along
// its normal and reaches `halfSize` either way across it. UV (0, 0), the
// top-left texel, goes on the top-left corner. Tangents are left empty;
// computeTangents fills them in.
void addFace(Mesh& mesh,
             Vec3 normal,
             Vec3 across1,
             Vec3 across2,
             float offset,
             float halfSize)
{
    const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    auto first = static_cast<std::uint16_t>(mesh.vertices.size());

    for (const auto& corner: corners)
    {
        auto position =
            normal * offset + (across1 * corner[0] + across2 * corner[1]) * halfSize;
        auto uv = Vec2 {(corner[0] + 1.0f) * 0.5f, (1.0f - corner[1]) * 0.5f};
        mesh.vertices.push_back({position, normal, uv, {}, {}});
    }

    for (auto i: {0, 1, 2, 0, 2, 3})
        mesh.indices.push_back(static_cast<std::uint16_t>(first + i));
}

// Tangent and bitangent from positions and UVs alone. Along a triangle's two
// edges, the position changes by e1 and e2 while the UV changes by (du1, dv1)
// and (du2, dv2). The tangent T and bitangent B are the position change per
// unit of u and of v, so
//     e1 = du1 * T + dv1 * B
//     e2 = du2 * T + dv2 * B
// two equations in two unknown vectors, solved with the 2x2 inverse. Each
// vertex sums the answers of the triangles it belongs to, and is then made
// orthonormal with the vertex normal (Gram-Schmidt).
void computeTangents(Mesh& mesh)
{
    auto tangents = std::vector<Vec3>(mesh.vertices.size());
    auto bitangents = std::vector<Vec3>(mesh.vertices.size());

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const auto i0 = mesh.indices[i];
        const auto i1 = mesh.indices[i + 1];
        const auto i2 = mesh.indices[i + 2];
        const auto& v0 = mesh.vertices[i0];
        const auto& v1 = mesh.vertices[i1];
        const auto& v2 = mesh.vertices[i2];

        auto e1 = v1.position - v0.position;
        auto e2 = v2.position - v0.position;
        auto du1 = v1.uv.x - v0.uv.x;
        auto dv1 = v1.uv.y - v0.uv.y;
        auto du2 = v2.uv.x - v0.uv.x;
        auto dv2 = v2.uv.y - v0.uv.y;

        auto determinant = du1 * dv2 - du2 * dv1;

        if (std::abs(determinant) < 1e-8f)
            continue; // degenerate UVs: this triangle has no texture direction

        auto r = 1.0f / determinant;
        auto t = (e1 * dv2 - e2 * dv1) * r;
        auto b = (e2 * du1 - e1 * du2) * r;

        for (auto index: {i0, i1, i2})
        {
            tangents[index] = tangents[index] + t;
            bitangents[index] = bitangents[index] + b;
        }
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        auto& vertex = mesh.vertices[i];
        const auto n = vertex.normal;

        // Remove any part of T along N, then any part of B along N and T.
        auto t = normalize(tangents[i] - n * dot(n, tangents[i]));
        auto b = bitangents[i] - n * dot(n, bitangents[i]);
        b = normalize(b - t * dot(t, b));

        vertex.tangent = t;
        vertex.bitangent = b;
    }
}

// The wall: one face through the origin, towards +z, 2.4 units across.
Mesh makeWall()
{
    auto mesh = Mesh {};
    addFace(mesh, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 0.0f, 1.2f);
    computeTangents(mesh);
    return mesh;
}

// The lamp: a unit cube, six faces. Only its positions are used.
Mesh makeCube()
{
    auto mesh = Mesh {};
    addFace(mesh, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}, 0.5f, 0.5f);
    addFace(mesh, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 0.5f, 0.5f);
    addFace(mesh, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}, 0.5f, 0.5f);
    addFace(mesh, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}, 0.5f, 0.5f);
    addFace(mesh, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 0.5f, 0.5f);
    addFace(mesh, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, 0.5f, 0.5f);
    return mesh;
}

// The hash and value noise from chapter 12.
float hash(int x, int y)
{
    auto h = static_cast<std::uint32_t>(x) * 374761393u
             + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h ^ (h >> 16)) / 4294967295.0f;
}

float noise(float x, float y)
{
    auto x0 = static_cast<int>(std::floor(x));
    auto y0 = static_cast<int>(std::floor(y));
    auto fx = x - static_cast<float>(x0);
    auto fy = y - static_cast<float>(y0);
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    auto top = hash(x0, y0) + (hash(x0 + 1, y0) - hash(x0, y0)) * fx;
    auto bottom = hash(x0, y0 + 1) + (hash(x0 + 1, y0 + 1) - hash(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

float smoothstep(float edge0, float edge1, float x)
{
    auto t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The brick layout, shared by both maps so they line up texel for texel.
constexpr auto mapSize = 512;
constexpr auto rows = 8;
constexpr auto brickHeight = mapSize / rows; // 64 texels
constexpr auto brickWidth = 2 * brickHeight; // 128 texels
constexpr auto mortar = 5.0f; // half the width of a mortar joint, in texels
constexpr auto bevel = 7.0f; // how far in from the joint a brick reaches full height

// Where texel (x, y) falls in the wall: which brick, and how far it is from
// the nearest joint. Odd rows are shifted half a brick, like a real wall.
struct BrickCell
{
    int column, row;
    float edgeDistance; // texels to the nearest mortar line
};

BrickCell brickAt(int x, int y)
{
    auto row = y / brickHeight;
    auto shifted = x + (row % 2) * (brickWidth / 2);
    auto column = shifted / brickWidth;

    auto inX = static_cast<float>(shifted % brickWidth) + 0.5f;
    auto inY = static_cast<float>(y % brickHeight) + 0.5f;
    auto toEdgeX = std::min(inX, static_cast<float>(brickWidth) - inX);
    auto toEdgeY = std::min(inY, static_cast<float>(brickHeight) - inY);

    return {column, row, std::min(toEdgeX, toEdgeY)};
}

// The wall's height at a texel, 0 deep in the mortar to 1 on a brick face:
// a rounded bevel up from each joint, a little roughness on the brick and a
// lot of it in the mortar.
float heightAt(int x, int y)
{
    x = (x + mapSize) % mapSize; // wrap, so the map tiles seamlessly
    y = (y + mapSize) % mapSize;

    auto cell = brickAt(x, y);
    auto fx = static_cast<float>(x);
    auto fy = static_cast<float>(y);

    auto rise = smoothstep(mortar, mortar + bevel, cell.edgeDistance);
    auto brickRough =
        0.06f * noise(fx * 0.15f, fy * 0.15f) + 0.03f * noise(fx * 0.6f, fy * 0.6f);
    auto mortarRough = 0.12f * noise(fx * 0.5f, fy * 0.5f);

    return rise * (0.9f + brickRough) + (1.0f - rise) * mortarRough;
}

std::uint8_t toByte(float v)
{
    return static_cast<std::uint8_t>(255.0f * std::clamp(v, 0.0f, 1.0f) + 0.5f);
}

struct WallMaps
{
    Texture diffuse;
    Texture normal;
};

WallMaps makeWallMaps()
{
    auto diffuse = std::vector<std::uint8_t>(mapSize * mapSize * 4);
    auto normals = std::vector<std::uint8_t>(mapSize * mapSize * 4);

    // How steep the bumps are: the height difference across one texel is
    // multiplied by this before it becomes a slope.
    constexpr auto strength = 6.0f;

    for (auto y = 0; y < mapSize; ++y)
    {
        for (auto x = 0; x < mapSize; ++x)
        {
            auto cell = brickAt(x, y);
            auto fx = static_cast<float>(x);
            auto fy = static_cast<float>(y);

            // The diffuse map: each brick its own shade of red-brown, mottled,
            // on light grey mortar.
            auto rise = smoothstep(mortar, mortar + bevel, cell.edgeDistance);
            auto tint = 0.75f + 0.45f * hash(cell.column, cell.row);
            auto mottle = 0.85f + 0.3f * noise(fx * 0.08f, fy * 0.08f);
            auto brick = Vec3 {0.62f, 0.27f, 0.17f} * tint * mottle;
            auto grout = Vec3 {0.62f, 0.60f, 0.56f}
                         * (0.85f + 0.2f * noise(fx * 0.5f, fy * 0.5f));
            auto colour = grout + (brick - grout) * rise;

            auto* d = &diffuse[(y * mapSize + x) * 4];
            d[0] = toByte(colour.x);
            d[1] = toByte(colour.y);
            d[2] = toByte(colour.z);
            d[3] = 255;

            // The normal map: the slope of the height in the u direction
            // (x, to the right) and the v direction (y, down the image), by
            // central differences. A surface rising to the right tilts its
            // normal to the left, hence the minus signs. In tangent space x is
            // along +u, y is along +v and z points out of the surface.
            auto dhdu = (heightAt(x + 1, y) - heightAt(x - 1, y)) * 0.5f;
            auto dhdv = (heightAt(x, y + 1) - heightAt(x, y - 1)) * 0.5f;
            auto n = normalize(Vec3 {-strength * dhdu, -strength * dhdv, 1.0f});

            // A unit vector's components are in -1..1; a texel's in 0..1.
            auto* m = &normals[(y * mapSize + x) * 4];
            m[0] = toByte(n.x * 0.5f + 0.5f);
            m[1] = toByte(n.y * 0.5f + 0.5f);
            m[2] = toByte(n.z * 0.5f + 0.5f);
            m[3] = 255;
        }
    }

    // RGBA8Unorm, not an sRGB format: the normal map holds directions, not
    // colours, and must come back exactly as it was written.
    auto descriptor = TextureDescriptor {};
    descriptor.width = mapSize;
    descriptor.height = mapSize;
    descriptor.format = TextureFormat::RGBA8Unorm;

    auto& device = Device::shared();
    return {device.makeTexture(descriptor, diffuse.data()),
            device.makeTexture(descriptor, normals.data())};
}

// The wall's shader. The vertex stage takes the normal, tangent and bitangent
// to world space; the fragment stage builds the TBN matrix from them, turns
// the normal map's tangent-space normal into a world-space one with it, and
// lights the fragment exactly as in chapter 09.
struct WallShader final : ShaderProgram
{
    WallShader()
    {
        diffuseMap.sampling = {TextureFilter::Linear, TextureAddressMode::Repeat};
        normalMap.sampling = {TextureFilter::Linear, TextureAddressMode::Repeat};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);
        auto tangent = vertexInput(&Vertex::tangent);
        auto bitangent = vertexInput(&Vertex::bitangent);

        auto worldPosition = (model * float4(position, 1.0f)).xyz();

        // The tangent and bitangent lie *in* the surface, like an edge of a
        // triangle, so the model matrix moves them. The normal needs the
        // normal matrix, as in chapter 09.
        auto worldTangent = (model * float4(tangent, 0.0f)).xyz();
        auto worldBitangent = (model * float4(bitangent, 0.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();

        auto clipPosition = viewProjection * float4(worldPosition, 1.0f);
        setPosition(clipPosition);

        auto fragmentPosition = varying(worldPosition);
        auto uv = varying(vertexInput(&Vertex::uv));
        auto t = normalize(varying(worldTangent));
        auto b = normalize(varying(worldBitangent));
        auto flatNormal = normalize(varying(worldNormal));

        // The TBN matrix's columns are the tangent-space axes written in world
        // space, so multiplying a tangent-space vector by it gives the same
        // vector in world space.
        auto tbn = float3x3(t, b, flatNormal);

        // Unpack the texel from 0..1 back to a -1..1 direction.
        auto tangentNormal = sample(normalMap, uv).xyz() * 2.0f - 1.0f;
        auto mappedNormal = normalize(tbn * tangentNormal);

        // Which normal to light with, by which side of the split this pixel is
        // on. Clip x divided by clip w is the pixel's x in normalized device
        // coordinates: -1 at the left edge of the window, +1 at the right.
        auto ndcX = varying(clipPosition.x()) / varying(clipPosition.w());
        auto n = select(ndcX < split, flatNormal, mappedNormal);

        // Chapter 09's Phong lighting, with the brick colour from the diffuse
        // map as the object colour.
        auto surfaceColor = sample(diffuseMap, uv).xyz();
        auto l = normalize(lightPosition - fragmentPosition);
        auto v = normalize(cameraPosition - fragmentPosition);
        auto r = reflect(-l, n);

        auto ambient = 0.08f;
        auto diffuse = max(dot(n, l), 0.0f);
        auto specular = 0.25f * pow(max(dot(v, r), 0.0f), shininess);

        // A point light fades with distance, so the lit patch follows it
        // across the wall.
        auto distance = length(lightPosition - fragmentPosition);
        auto attenuation = 1.0f / (1.0f + 0.2f * distance * distance);

        auto lit = ambient * surfaceColor
                   + attenuation * (diffuse * surfaceColor + specular) * lightColor;

        // A thin bright line marks the split.
        auto onLine = abs(ndcX - split) < 0.004f;
        setFragment(float4(select(onLine, lightColor, lit), 1.0f));
    }

    static constexpr float shininess = 32.0f;

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> lightPosition;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> lightColor;
    Uniform<Float> split; // pixels left of this NDC x use the flat normal
    Uniform<Texture2D> diffuseMap;
    Uniform<Texture2D> normalMap;

    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                lightPosition,
                cameraPosition,
                lightColor,
                split,
                diffuseMap,
                normalMap)
};

// The lamp from chapter 09.
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

struct NormalMappingView final : GPUView
{
    NormalMappingView()
    {
        setDepth(true);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = true;
        pipeline.cullMode = CullMode::Back;

        wallShader.setVertices(wall.vertices);
        wallShader.setIndices(wall.indices.data(), (int) wall.indices.size());
        wallShader.prepare(pipeline);
        wallShader.diffuseMap = maps.diffuse;
        wallShader.normalMap = maps.normal;

        lampShader.setVertices(cube.vertices);
        lampShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        lampShader.prepare(pipeline);

        setContinuous(true);
    }

    // Space cycles: split down the middle, all flat, all normal-mapped.
    void keyDown(const Graphics::KeyEvent& event) override
    {
        if (event.keyCode != Graphics::KeyCode::Space || event.modifiers.command)
        {
            passKeyOn();
            return;
        }

        mode = (mode + 1) % 3;
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        lightAngle += 0.8f * delta;
        wallClock += delta;
    }

    void render(Frame& frame) override
    {
        // The light circles in front of the wall, close to it, so it rakes
        // across the bricks at a low angle where bumps show best.
        auto lightPosition =
            Vec3 {0.8f * std::cos(lightAngle), 0.6f * std::sin(lightAngle), 0.5f};
        auto lightColor = Vec3 {1.0f, 0.95f, 0.85f};
        auto cameraPosition = Vec3 {0.0f, 0.0f, 3.6f};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 0, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        // The wall turns gently from side to side, which shows that the
        // tangent frame turns with it.
        auto model = Mat4::rotationY(0.35f * std::sin(0.4f * wallClock));

        const float splits[] = {0.0f, 2.0f, -2.0f};

        wallShader.model = model;
        wallShader.normalMatrix = model.inverted().transposed();
        wallShader.viewProjection = viewProjection;
        wallShader.lightPosition = lightPosition;
        wallShader.cameraPosition = cameraPosition;
        wallShader.lightColor = lightColor;
        wallShader.split = splits[mode];

        auto lampModel = Mat4::translation(lightPosition) * Mat4::scale(0.06f);
        lampShader.modelViewProjection = viewProjection * lampModel;
        lampShader.color = lightColor;

        auto pass = frame.beginPass({Graphics::Color {0.05f, 0.05f, 0.07f}});
        pass.draw(wallShader);
        pass.draw(lampShader);
    }

    // Declared before the shaders, so the textures outlive them.
    WallMaps maps = makeWallMaps();
    Mesh wall = makeWall();
    Mesh cube = makeCube();
    WallShader wallShader;
    LampShader lampShader;
    int mode = 0;
    float lightAngle = 0.6f;
    float wallClock = 0.0f;
};
} // namespace NormalMapping

LEARNEACP_CHAPTER(NormalMapping::NormalMappingView)
