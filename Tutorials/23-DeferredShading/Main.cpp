// 23. Deferred Shading: a 9x9 grid of cubes on a floor, lit by 96 small
// coloured point lights that orbit between them. The geometry is drawn into a
// G-buffer (world position, normal, albedo + specular) with no lighting at
// all, then a single full-screen pass reads it back and loops over every light
// from a storage buffer, skipping any light whose radius does not reach the
// pixel. The light markers are drawn afterwards, forward, against the
// G-buffer's own depth. A row of insets along the bottom shows each G-buffer
// attachment; a mouse click hides or shows it.
#include <eacp/GPU/GPU.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

using namespace eacp;
using namespace GPU;
using namespace Maths;

struct Vertex
{
    Vec3 position;
    Vec3 normal;
};

struct QuadVertex
{
    Vec2 position;
    Vec2 uv;
};

// One light as the lighting shader reads it: two Float4s back to back, so
// record i is read4(2 * i) and read4(2 * i + 1). The radius rides in the w of
// the first; the second w is padding that keeps every record 16-byte aligned.
struct PointLight
{
    Vec3 position;
    float radius;
    Vec3 color;
    float padding;
};

static_assert(sizeof(PointLight) == 8 * sizeof(float));

namespace
{
constexpr auto lightCount = 96;
constexpr auto gridSize = 9;
constexpr auto gridSpacing = 1.8f;

// The attenuation every light shares: constant 1, then these two. They give a
// light of brightness 1 a reach of about 5 units.
constexpr auto attenuationLinear = 0.7f;
constexpr auto attenuationQuadratic = 1.8f;

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

// The full-screen quad from chapter 16: clip-space y = +1 reads v = 0.
const QuadVertex screenQuad[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{1.0f, -1.0f}, {1.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 0.0f}},
    {{-1.0f, 1.0f}, {0.0f, 0.0f}},
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

// The three things the lighting pass needs to know about each pixel, one
// texture each.
enum class Attachment
{
    Position, // world-space position; w = 1 where there is geometry
    Normal, // world-space unit normal
    AlbedoSpecular, // surface colour, and specular strength in w
};

constexpr auto attachmentCount = 3;

constexpr TextureFormat formatOf(Attachment attachment)
{
    switch (attachment)
    {
        case Attachment::Position:
            return TextureFormat::RGBA32Float;
        case Attachment::Normal:
            return TextureFormat::RGBA16Float;
        case Attachment::AlbedoSpecular:
            return TextureFormat::RGBA8Unorm;
    }

    return TextureFormat::RGBA8Unorm;
}

// The geometry pass. It does no lighting at all: it works out the surface
// attributes at each pixel and stores them. The Attachment chosen at
// construction decides which one this program writes, the way chapter 16's
// Effect chose a post-processing graph.
struct GeometryShader final : ShaderProgram
{
    explicit GeometryShader(Attachment attachmentToWrite)
        : attachment(attachmentToWrite)
    {
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

        switch (attachment)
        {
            case Attachment::Position:
                setFragment(float4(fragmentPosition, 1.0f));
                break;

            case Attachment::Normal:
                setFragment(float4(normalize(varying(worldNormal)), 0.0f));
                break;

            case Attachment::AlbedoSpecular:
            {
                // The checkerboard from chapter 16, so the floor has detail.
                auto square = mod(
                    floor(fragmentPosition.x()) + floor(fragmentPosition.z()), 2.0f);
                auto albedo = objectColor * (1.0f - checker * 0.4f * square);
                setFragment(float4(albedo, specular));
                break;
            }
        }
    }

    Attachment attachment;

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> objectColor;
    Uniform<Float> specular;
    Uniform<Float> checker;

    EACP_SHADER(model, normalMatrix, viewProjection, objectColor, specular, checker)
};

// The lighting pass: one full-screen quad, and for every pixel a loop over
// every light. Nothing here knows about cubes or floors; it sees only the
// G-buffer.
struct LightingShader final : ShaderProgram
{
    LightingShader()
    {
        // One texel per pixel, read at its centre, so nearest is exact. It
        // also matters for position: blending two neighbours' positions across
        // a cube's silhouette would invent a point in mid-air.
        for (auto* texture: {&gPosition, &gNormal, &gAlbedoSpecular})
            texture->sampling = {TextureFilter::Nearest, TextureAddressMode::Clamp};

        compile();
    }

    void define() override
    {
        auto position = vertexInput(&QuadVertex::position);
        auto uv = varying(vertexInput(&QuadVertex::uv));
        setPosition(float4(position, 0.0f, 1.0f));

        // Unpack the G-buffer: everything the forward shader got from its
        // vertex stage and uniforms, now read back from three textures.
        auto stored = sample(gPosition, uv);
        auto surfacePosition = stored.xyz();
        auto n = normalize(sample(gNormal, uv).xyz());
        auto albedoSpecular = sample(gAlbedoSpecular, uv);
        auto albedo = albedoSpecular.xyz();
        auto specularStrength = albedoSpecular.w();

        auto v = normalize(cameraPosition - surfacePosition);

        auto color = var(albedo * 0.005f);

        // A loop that really runs on the GPU, counting to a uniform: the light
        // count can change without recompiling the shader.
        auto i = var(0u);

        loop(i < lightCount,
             [&]
             {
                 auto record = lights.read4(i.get() * 2u);
                 auto lightColor = lights.read4(i.get() * 2u + 1u).xyz();

                 auto toLight = record.xyz() - surfacePosition;
                 auto distance = length(toLight);

                 // The light volume test: beyond its radius a light's
                 // contribution is too small to see, so skip the maths.
                 ifThen(distance < record.w(),
                        [&]
                        {
                            auto l = toLight / distance;
                            auto h = normalize(l + v);

                            auto diffuse = max(dot(n, l), 0.0f);
                            auto specular =
                                specularStrength * pow(max(dot(n, h), 0.0f), 32.0f);
                            auto attenuation =
                                1.0f
                                / (1.0f + attenuationLinear * distance
                                   + attenuationQuadratic * distance * distance);

                            color = color.get()
                                    + (diffuse * albedo + specular) * lightColor
                                          * attenuation;
                        });

                 i += 1u;
             });

        // Pixels the geometry pass never touched still hold the clear value,
        // w = 0: those get the background colour instead of lighting.
        auto lit = select(stored.w() > 0.5f, color.get(), background);
        setFragment(float4(lit, 1.0f));
    }

    Uniform<Texture2D> gPosition;
    Uniform<Texture2D> gNormal;
    Uniform<Texture2D> gAlbedoSpecular;
    Uniform<InputBuffer> lights;
    Uniform<UInt> lightCount;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> background;

    EACP_SHADER(gPosition,
                gNormal,
                gAlbedoSpecular,
                lights,
                lightCount,
                cameraPosition,
                background)
};

// The light markers, drawn forward after the lighting pass: the same cube,
// unlit, in the light's colour.
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

// Draws a texture into a rectangle of the window: the lit image over the whole
// of it, or one G-buffer attachment in a small inset. rect is the rectangle in
// clip space (left, bottom, right, top).
enum class Show
{
    Lit, // the final image, tone mapped
    Position, // world position, scaled into 0..1
    Normal, // n * 0.5 + 0.5, the usual way to look at a normal
    Albedo, // the surface colour
    Specular, // the specular strength, as grey
};

struct DisplayShader final : ShaderProgram
{
    explicit DisplayShader(Show showToUse)
        : show(showToUse)
    {
        image.sampling = {TextureFilter::Linear, TextureAddressMode::Clamp};
        compile();
    }

    void define() override
    {
        auto corner = vertexInput(&QuadVertex::position) * 0.5f + 0.5f;
        auto uv = varying(vertexInput(&QuadVertex::uv));

        auto clip = rect.xy() + corner * (rect.zw() - rect.xy());
        setPosition(float4(clip, 0.0f, 1.0f));

        auto texel = sample(image, uv);
        setFragment(float4(present(texel), 1.0f));
    }

    Float3 present(const Float4& texel)
    {
        switch (show)
        {
            case Show::Lit:
            {
                // Many lights add up past 1. Chapter 22's exposure curve maps
                // that into the displayable range instead of clipping it, and
                // its gamma step encodes the linear result for the display.
                auto mapped = 1.0f - exp(-texel.xyz() * exposure);
                return pow(mapped, 1.0f / 2.2f);
            }
            case Show::Position:
                return clamp(texel.xyz() / 20.0f + 0.5f, 0.0f, 1.0f);
            case Show::Normal:
                return texel.xyz() * 0.5f + 0.5f;
            case Show::Albedo:
                return texel.xyz();
            case Show::Specular:
                return float3(texel.w(), texel.w(), texel.w());
        }

        return texel.xyz();
    }

    static constexpr float exposure = 0.8f;

    Show show;

    Uniform<Texture2D> image;
    Uniform<Float4> rect;

    EACP_SHADER(image, rect)
};

// A small deterministic random number generator, so the lights are the same
// every run.
float random01(unsigned& state)
{
    state = state * 1664525u + 1013904223u;
    return (float) (state >> 8) / 16777216.0f;
}

// A fully saturated colour from a hue in 0..1, red through green and blue
// back to red.
Vec3 hueToColor(float hue)
{
    auto channel = [&](float offset)
    {
        auto x = std::fabs(std::fmod(hue * 6.0f + offset, 6.0f) - 3.0f) - 1.0f;
        return std::clamp(x, 0.0f, 1.0f);
    };

    return {channel(0.0f), channel(4.0f), channel(2.0f)};
}

// How a light moves: a circle around the middle of the scene, at its own
// radius, height, speed and starting angle.
struct Orbit
{
    float radius, height, speed, phase;
    Vec3 color;
};

std::array<Orbit, lightCount> makeOrbits()
{
    auto orbits = std::array<Orbit, lightCount> {};
    auto state = 12345u;

    for (auto& orbit: orbits)
    {
        orbit.radius = 1.0f + 8.5f * std::sqrt(random01(state));
        orbit.height = 0.5f + 2.0f * random01(state);
        auto direction = random01(state) < 0.5f ? -1.0f : 1.0f;
        orbit.speed = direction * (0.15f + 0.35f * random01(state));
        orbit.phase = 6.2831853f * random01(state);
        orbit.color = hueToColor(random01(state)) * 1.6f;
    }

    return orbits;
}

// The distance at which a light of this colour has faded to 5/256 of its
// brightness: solve max(colour) / (1 + linear d + quadratic d^2) = 5/256 for d.
// Past that, its contribution rounds away in an 8-bit image.
float lightRadius(const Vec3& color)
{
    auto brightest = std::max({color.x, color.y, color.z});
    auto c = 1.0f - brightest * (256.0f / 5.0f);
    auto l = attenuationLinear;
    auto q = attenuationQuadratic;

    return (-l + std::sqrt(l * l - 4.0f * q * c)) / (2.0f * q);
}
} // namespace

struct DeferredShadingView final : GPUView
{
    DeferredShadingView()
    {
        // The geometry passes draw into G-buffer textures and test against a
        // depth buffer, all single-sampled.
        for (auto i = 0; i < attachmentCount; ++i)
        {
            auto pipeline = RenderPipelineDescriptor {};
            pipeline.sampleCount = 1;
            pipeline.depth = true;
            pipeline.cullMode = CullMode::Back;
            pipeline.colorFormat =
                pixelFormatFor(formatOf(geometryShaders[i].attachment));

            geometryShaders[i].setVertices(cube.vertices);
            geometryShaders[i].setIndices(cube.indices.data(),
                                          (int) cube.indices.size());
            geometryShaders[i].prepare(pipeline);
        }

        // The lighting quad draws into the lit target, which carries the
        // geometry's depth buffer. It must cover every pixel, so it ignores
        // the depth and leaves it untouched for the markers.
        auto lightingPipeline = RenderPipelineDescriptor {};
        lightingPipeline.sampleCount = 1;
        lightingPipeline.depth = true;
        lightingPipeline.depthCompare = DepthCompare::Always;
        lightingPipeline.depthWrite = false;
        lightingPipeline.colorFormat = pixelFormatFor(litFormat);

        lightingShader.setVertices(screenQuad);
        lightingShader.setIndices(quadIndices);
        lightingShader.prepare(lightingPipeline);

        // The markers are ordinary depth-tested geometry in the same pass.
        auto lampPipeline = RenderPipelineDescriptor {};
        lampPipeline.sampleCount = 1;
        lampPipeline.depth = true;
        lampPipeline.cullMode = CullMode::Back;
        lampPipeline.colorFormat = pixelFormatFor(litFormat);

        lampShader.setVertices(cube.vertices);
        lampShader.setIndices(cube.indices.data(), (int) cube.indices.size());
        lampShader.prepare(lampPipeline);

        for (auto& display: displayShaders)
        {
            display.setVertices(screenQuad);
            display.setIndices(quadIndices);
            display.prepare(sampleCount());
        }

        // A click toggles the G-buffer insets.
        setHandlesMouseEvents(true);
        setContinuous(true);
    }

    // Recreates the G-buffer and the lit target whenever the drawable's size
    // changes, as chapter 16 did for its one offscreen texture.
    void resizeTargets(int width, int height)
    {
        if (lit && width == targetWidth && height == targetHeight)
            return;

        targetWidth = width;
        targetHeight = height;

        auto descriptor = TextureDescriptor {};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.renderTarget = true;

        for (auto i = 0; i < attachmentCount; ++i)
        {
            descriptor.format = formatOf((Attachment) i);
            gBuffer[i].emplace(Device::shared().makeTexture(descriptor));
        }

        // The lit target owns the one depth buffer every pass shares.
        descriptor.format = litFormat;
        descriptor.depth = true;
        lit.emplace(Device::shared().makeTexture(descriptor));

        auto& position = *gBuffer[(int) Attachment::Position];
        auto& normal = *gBuffer[(int) Attachment::Normal];
        auto& albedoSpecular = *gBuffer[(int) Attachment::AlbedoSpecular];

        lightingShader.gPosition = position;
        lightingShader.gNormal = normal;
        lightingShader.gAlbedoSpecular = albedoSpecular;

        displayShaders[(int) Show::Lit].image = *lit;
        displayShaders[(int) Show::Position].image = position;
        displayShaders[(int) Show::Normal].image = normal;
        displayShaders[(int) Show::Albedo].image = albedoSpecular;
        displayShaders[(int) Show::Specular].image = albedoSpecular;
    }

    void update(Threads::FrameTime time) override
    {
        elapsed += static_cast<float>(time.delta);
    }

    void mouseDown(const Graphics::MouseEvent&) override
    {
        showGBuffer = !showGBuffer;
    }

    void render(Frame& frame) override
    {
        auto size = frame.pixelSize();
        auto width = (int) size.x;
        auto height = (int) size.y;

        if (width <= 0 || height <= 0)
            return;

        resizeTargets(width, height);

        auto cameraAngle = 0.6f + 0.05f * elapsed;
        auto cameraPosition = Vec3 {
            13.0f * std::sin(cameraAngle), 8.0f, 13.0f * std::cos(cameraAngle)};
        auto view = Mat4::lookAt(cameraPosition, {0.0f, -1.5f, 0.0f}, {0, 1, 0});
        auto projection = Mat4::perspective(
            (float) width / (float) height, radians(45.0f), 0.1f, 100.0f);
        auto viewProjection = projection * view;

        // This frame's light positions. They change every frame, so they go
        // into a fresh slice of a streaming buffer each time rather than over
        // a buffer the previous frame may still be reading.
        auto lights = std::array<PointLight, lightCount> {};

        for (auto i = 0; i < lightCount; ++i)
        {
            const auto& orbit = orbits[i];
            auto angle = orbit.phase + orbit.speed * elapsed;
            auto bob = 0.2f * std::sin(1.3f * elapsed + orbit.phase * 3.0f);

            lights[i].position = {orbit.radius * std::cos(angle),
                                  orbit.height + bob,
                                  orbit.radius * std::sin(angle)};
            lights[i].color = orbit.color;
            lights[i].radius = radii[i];
        }

        // Passes 1 to 3, the geometry passes: one per G-buffer attachment,
        // each drawing the whole scene. They all borrow the lit target's depth
        // buffer, clearing it on the way in and keeping it on the way out, so
        // what the last one leaves there is the scene's depth.
        for (auto i = 0; i < attachmentCount; ++i)
        {
            auto descriptor = RenderPassDescriptor {};
            descriptor.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            descriptor.depthAction = DepthAction::Keep;
            descriptor.depthTarget = {&*lit};

            auto pass = frame.beginPass(*gBuffer[i], descriptor);
            drawScene(pass, geometryShaders[i], viewProjection);
        }

        // Pass 4, the lighting pass, into the lit target. Resuming its depth
        // buffer means the markers drawn after the quad are hidden by the
        // cubes exactly as if the whole scene had been drawn forward.
        {
            auto descriptor = RenderPassDescriptor {};
            descriptor.depthAction = DepthAction::Resume;

            auto pass = frame.beginPass(*lit, descriptor);

            lightingShader.lights = lightStream.write(lights.data(), sizeof(lights));
            lightingShader.lightCount = (unsigned) lightCount;
            lightingShader.cameraPosition = cameraPosition;
            lightingShader.background = Vec3 {0.001f, 0.001f, 0.0015f};
            pass.draw(lightingShader);

            for (const auto& light: lights)
            {
                auto model = Mat4::translation(light.position) * Mat4::scale(0.12f);
                lampShader.modelViewProjection = viewProjection * model;
                lampShader.color = light.color;
                pass.draw(lampShader);
            }
        }

        // Pass 5, into the window: the lit image, and the G-buffer insets.
        auto pass = frame.beginPass();

        auto& final = displayShaders[(int) Show::Lit];
        final.rect = Vec4 {-1.0f, -1.0f, 1.0f, 1.0f};
        pass.draw(final);

        if (showGBuffer)
        {
            // Four insets along the bottom, each a quarter of the width minus
            // a margin, keeping the window's aspect ratio.
            auto margin = 0.02f;
            auto insetWidth = (2.0f - 5.0f * margin) / 4.0f;
            auto insetHeight = insetWidth;

            for (auto i = 0; i < 4; ++i)
            {
                auto& inset = displayShaders[1 + i];
                auto left = -1.0f + margin + (float) i * (insetWidth + margin);
                inset.rect = Vec4 {left,
                                   -1.0f + margin,
                                   left + insetWidth,
                                   -1.0f + margin + insetHeight};
                pass.draw(inset);
            }
        }
    }

    // The scene: a floor and a grid of cubes of varying height and turn. The
    // one geometry program given draws all of it, writing its attachment.
    void drawScene(RenderPass& pass,
                   GeometryShader& shader,
                   const Mat4& viewProjection)
    {
        shader.viewProjection = viewProjection;

        auto drawObject =
            [&](const Mat4& model, Vec3 color, float specular, float checker)
        {
            shader.model = model;
            shader.normalMatrix = model.inverted().transposed();
            shader.objectColor = color;
            shader.specular = specular;
            shader.checker = checker;
            pass.draw(shader);
        };

        drawObject(Mat4::translation({0.0f, -0.05f, 0.0f})
                       * Mat4::scale({22.0f, 0.1f, 22.0f}),
                   {0.75f, 0.75f, 0.72f},
                   0.3f,
                   1.0f);

        auto offset = (float) (gridSize - 1) * 0.5f;

        for (auto row = 0; row < gridSize; ++row)
        {
            for (auto column = 0; column < gridSize; ++column)
            {
                auto x = ((float) column - offset) * gridSpacing;
                auto z = ((float) row - offset) * gridSpacing;
                auto height = 0.5f + 0.35f * (float) ((row * 7 + column * 3) % 4);
                auto angle = 0.4f * (float) ((row * 5 + column * 2) % 5);
                auto shine = 0.1f + 0.3f * (float) ((row + column * 2) % 4);

                drawObject(Mat4::translation({x, height * 0.5f, z})
                               * Mat4::rotationY(angle)
                               * Mat4::scale({0.7f, height, 0.7f}),
                           {0.85f, 0.83f, 0.8f},
                           shine,
                           0.0f);
            }
        }
    }

    static constexpr auto litFormat = TextureFormat::RGBA16Float;

    CubeMesh cube = makeCube();
    std::array<Orbit, lightCount> orbits = makeOrbits();
    std::array<float, lightCount> radii = [this]
    {
        auto result = std::array<float, lightCount> {};

        for (auto i = 0; i < lightCount; ++i)
            result[i] = lightRadius(orbits[i].color);

        return result;
    }();

    StreamingBuffers lightStream {BufferUsage::Storage};

    // Declared before the shaders that point at them, so they outlive them.
    std::array<std::optional<Texture>, attachmentCount> gBuffer;
    std::optional<Texture> lit;
    int targetWidth = 0;
    int targetHeight = 0;

    std::array<GeometryShader, attachmentCount> geometryShaders {
        GeometryShader {Attachment::Position},
        GeometryShader {Attachment::Normal},
        GeometryShader {Attachment::AlbedoSpecular}};

    LightingShader lightingShader;
    LampShader lampShader;

    std::array<DisplayShader, 5> displayShaders {DisplayShader {Show::Lit},
                                                 DisplayShader {Show::Position},
                                                 DisplayShader {Show::Normal},
                                                 DisplayShader {Show::Albedo},
                                                 DisplayShader {Show::Specular}};

    bool showGBuffer = true;
    float elapsed = 0.0f;
};

int main()
{
    return Graphics::runWindowedApp<DeferredShadingView>();
}
