// 12. Lighting Maps: the Phong lighting of chapter 09, with the surface's
// properties grouped into a material and the light's into a light, and the
// material's flat colours replaced by two textures. A diffuse map paints a
// wooden crate with a steel frame; a specular map says how shiny each texel is,
// so the orbiting light glints off the steel and barely shows on the wood.
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace LightingMaps
{
// The vertex of chapter 09 plus a texture coordinate.
struct Vertex
{
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

namespace
{
struct CubeMesh
{
    std::array<Vertex, 24> vertices;
    std::array<std::uint16_t, 36> indices;
};

// Chapter 09's cube, face by face, with each face also given the whole texture:
// UV (0, 0), the top-left texel, on the corner that is top-left seen from
// outside, where across1 points right and across2 points up.
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
            auto uv =
                Vec2 {(corners[c][0] + 1.0f) * 0.5f, (1.0f - corners[c][1]) * 0.5f};
            mesh.vertices[f * 4 + c] = {position, face.normal, uv};
        }

        auto first = static_cast<std::uint16_t>(f * 4);
        const std::uint16_t quad[] = {0, 1, 2, 0, 2, 3};

        for (auto i = 0; i < 6; ++i)
            mesh.indices[f * 6 + i] = first + quad[i];
    }

    return mesh;
}

// A pseudo-random number in 0..1 for every integer grid point, the same every
// time it is asked for.
float hash(int x, int y)
{
    auto h = static_cast<std::uint32_t>(x) * 374761393u
             + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h ^ (h >> 16)) / 4294967295.0f;
}

// Value noise: the hashes at the four surrounding grid points, blended
// smoothly. Stretching x and y by different amounts gives streaks, which is
// all wood grain and brushed metal need.
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

// The two maps describe one surface, so they are generated together, from the
// same layout, and line up texel for texel.
struct CrateMaps
{
    Texture diffuse;
    Texture specular;
};

CrateMaps makeCrateMaps()
{
    constexpr auto size = 256;
    constexpr auto frame = 26; // width of the steel frame, in texels
    constexpr auto planks = 4;
    constexpr auto plankHeight = (size - 2 * frame) / planks;

    auto diffuse = std::vector<std::uint8_t>(size * size * 4);
    auto specular = std::vector<std::uint8_t>(size * size * 4);

    for (auto y = 0; y < size; ++y)
    {
        for (auto x = 0; x < size; ++x)
        {
            auto fx = static_cast<float>(x);
            auto fy = static_cast<float>(y);

            auto edge =
                std::min(std::min(x, y), std::min(size - 1 - x, size - 1 - y));
            auto inFrame = edge < frame;

            // A rivet at each corner and half way along each side of the frame.
            auto rivetCentre = [&](int v)
            {
                return v < size / 4       ? frame / 2
                       : v < 3 * size / 4 ? size / 2
                                          : size - frame / 2;
            };
            auto rivetX = fx - static_cast<float>(rivetCentre(x));
            auto rivetY = fy - static_cast<float>(rivetCentre(y));
            auto rivet = inFrame && rivetX * rivetX + rivetY * rivetY < 36.0f;

            Vec3 colour;
            float shine;

            if (rivet)
            {
                // Domed: lighter towards the top-left, as if lit from there.
                auto dome = 0.85f - 0.04f * (rivetX + rivetY);
                colour = Vec3 {0.62f, 0.64f, 0.67f} * dome;
                shine = 1.0f;
            }
            else if (inFrame)
            {
                // Brushed steel: streaks running along each side of the frame,
                // and a dark groove where the frame meets the wood.
                auto horizontal = std::min(y, size - 1 - y) < frame;
                auto brushed = horizontal ? noise(fx * 0.04f, fy * 1.7f)
                                          : noise(fx * 1.7f, fy * 0.04f);
                auto groove = edge >= frame - 3 ? 0.45f : 1.0f;
                colour =
                    Vec3 {0.36f, 0.38f, 0.42f} * (0.8f + 0.3f * brushed) * groove;
                shine = (0.65f + 0.3f * brushed) * groove;
            }
            else
            {
                // Planks running left to right. Each has its own tint, the
                // grain is a ripple across it bent by stretched noise, and a
                // thin dark gap separates neighbours.
                auto row = y - frame;
                auto plank = std::min(row / plankHeight, planks - 1);
                auto withinPlank = row - plank * plankHeight;
                auto gap = withinPlank < 2;

                auto tint = 0.82f + 0.3f * hash(plank, 7);
                auto bend = 5.0f * noise(fx * 0.025f, fy * 0.08f + plank * 9.0f);
                auto grain = 0.5f + 0.5f * std::sin(fy * 0.8f + bend * 2.0f);
                auto fibre = noise(fx * 0.3f, fy * 3.0f);

                colour = Vec3 {0.58f, 0.36f, 0.18f} * tint
                         * (0.7f + 0.22f * grain + 0.12f * fibre);
                shine = 0.04f + 0.06f * grain;

                if (gap)
                {
                    colour = colour * 0.25f;
                    shine = 0.0f;
                }
            }

            auto toByte = [](float v)
            {
                return static_cast<std::uint8_t>(255.0f * std::clamp(v, 0.0f, 1.0f));
            };

            auto* d = &diffuse[(y * size + x) * 4];
            d[0] = toByte(colour.x);
            d[1] = toByte(colour.y);
            d[2] = toByte(colour.z);
            d[3] = 255;

            // The specular map is grey: the same strength in all three channels.
            auto* s = &specular[(y * size + x) * 4];
            s[0] = s[1] = s[2] = toByte(shine);
            s[3] = 255;
        }
    }

    auto descriptor = TextureDescriptor {};
    descriptor.width = size;
    descriptor.height = size;
    descriptor.format = TextureFormat::RGBA8Unorm;

    auto& device = Device::shared();
    return {device.makeTexture(descriptor, diffuse.data()),
            device.makeTexture(descriptor, specular.data())};
}

// What the surface is made of. In chapter 09 this was one colour and a
// constant; now each property is a uniform, and the two colours are textures,
// so they can change from texel to texel.
struct Material
{
    Uniform<Texture2D> diffuse; // the surface's colour (ambient uses it too)
    Uniform<Texture2D> specular; // how strongly each texel reflects highlights
    Uniform<Float> shininess; // how tight those highlights are
};

// What the light gives off. Separate strengths for each Phong term let a scene
// have a dim ambient glow and a bright highlight from the same lamp.
struct Light
{
    Uniform<Float3> position;
    Uniform<Float3> ambient;
    Uniform<Float3> diffuse;
    Uniform<Float3> specular;
};

struct LitShader final : ShaderProgram
{
    LitShader()
    {
        // Sampling is fixed when the shader is compiled. Linear filtering
        // suits wood and metal; nothing reads outside 0..1, so the wrap mode
        // does not matter here.
        material.diffuse.sampling = {TextureFilter::Linear,
                                     TextureAddressMode::Repeat};
        material.specular.sampling = {TextureFilter::Linear,
                                      TextureAddressMode::Repeat};
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * float4(worldPosition, 1.0f));

        auto fragmentPosition = varying(worldPosition);
        auto n = normalize(varying(worldNormal));
        auto uv = varying(vertexInput(&Vertex::uv));

        // The material at this fragment: one read from each map.
        auto surfaceColor = sample(material.diffuse, uv).xyz();
        auto surfaceShine = sample(material.specular, uv).xyz();

        auto l = normalize(light.position - fragmentPosition);
        auto v = normalize(cameraPosition - fragmentPosition);
        auto r = reflect(-l, n);

        // Phong, term by term, each the light's strength times the
        // material's response.
        auto ambient = light.ambient * surfaceColor;
        auto diffuse = light.diffuse * max(dot(n, l), 0.0f) * surfaceColor;
        auto specular = light.specular
                        * pow(max(dot(v, r), 0.0f), material.shininess)
                        * surfaceShine;

        setFragment(float4(ambient + diffuse + specular, 1.0f));
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> cameraPosition;
    Material material;
    Light light;

    // EACP_SHADER lists the uniforms one by one; the members of a group are
    // listed by their full name.
    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                cameraPosition,
                material.diffuse,
                material.specular,
                material.shininess,
                light.position,
                light.ambient,
                light.diffuse,
                light.specular)
};

// The lamp from chapter 09: the same cube, unlit, in the light's colour.
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

struct LightingMapsView final : GPUView
{
    LightingMapsView()
    {
        setDepth(true);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = true;
        pipeline.cullMode = CullMode::Back;

        crateShader.setVertices(cube.vertices);
        crateShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        crateShader.prepare(pipeline);

        // The shader only points at the textures, which live in the view.
        crateShader.material.diffuse = maps.diffuse;
        crateShader.material.specular = maps.specular;
        crateShader.material.shininess = 32.0f;

        lampShader.setVertices(cube.vertices);
        lampShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        lampShader.prepare(pipeline);

        setContinuous(true);
    }

    void update(Threads::FrameTime time) override
    {
        auto delta = static_cast<float>(time.delta);
        lightAngle += 0.45f * delta;
        cubeAngle += 0.25f * delta;
    }

    void render(Frame& frame) override
    {
        auto lightPosition =
            Vec3 {1.4f * std::cos(lightAngle), 0.3f, 1.4f * std::sin(lightAngle)};
        auto lightColor = Vec3 {1.0f, 0.96f, 0.88f};
        auto cameraPosition = Vec3 {0.0f, 1.0f, 3.0f};

        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;
        auto view = Mat4::lookAt(cameraPosition, {0, 0, 0}, {0, 1, 0});
        auto projection = Mat4::perspective(aspect, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        auto model = Mat4::rotationY(cubeAngle) * Mat4::rotationX(0.2f);

        crateShader.model = model;
        crateShader.normalMatrix = model.inverted().transposed();
        crateShader.viewProjection = viewProjection;
        crateShader.cameraPosition = cameraPosition;

        // One light colour, scaled per term: a faint ambient, a strong
        // diffuse, and a full-strength specular.
        crateShader.light.position = lightPosition;
        crateShader.light.ambient = lightColor * 0.15f;
        crateShader.light.diffuse = lightColor * 0.8f;
        crateShader.light.specular = lightColor * 1.0f;

        auto lampModel = Mat4::translation(lightPosition) * Mat4::scale(0.1f);
        lampShader.modelViewProjection = viewProjection * lampModel;
        lampShader.color = lightColor;

        auto pass = frame.beginPass({Graphics::Color {0.06f, 0.06f, 0.08f}});
        pass.draw(crateShader);
        pass.draw(lampShader);
    }

    // Declared before the shaders, so the textures outlive them.
    CrateMaps maps = makeCrateMaps();
    CubeMesh cube = makeCube();
    LitShader crateShader;
    LampShader lampShader;
    // Starting angles picked so a glint crosses the steel in the first seconds.
    float lightAngle = 0.85f;
    float cubeAngle = 1.0f;
};
} // namespace LightingMaps

LEARNEACP_CHAPTER(LightingMaps::LightingMapsView)
