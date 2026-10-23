// Chapter 06: Textures. A small image is made on the CPU, uploaded to the GPU,
// and sampled by the fragment shader. The same texture is drawn twice with two
// different samplers - nearest + repeat on the left, linear + clamp on the
// right - and each sample is multiplied by a colour interpolated from the
// vertices.
#include <eacp/GPU/GPU.h>

#include <cstdint>
#include <vector>

using namespace eacp;
using namespace GPU;
using namespace Maths;

namespace
{
struct Vertex
{
    Vec2 position;
    Vec2 uv; // where in the texture this corner reads from
    Vec3 tint; // multiplied into the sample
};

constexpr auto white = Vec3 {1.0f, 1.0f, 1.0f};
constexpr auto orange = Vec3 {1.0f, 0.55f, 0.25f};

// Two quads side by side. Their UVs run 0..2, past the texture's 0..1 edge, so
// what the sampler does outside the texture is on show. UV (0, 0) is the
// texture's first texel, row 0, which is the top of the image, so it goes on
// the top-left corner.
const Vertex leftQuad[] = {
    {{-0.9f, -0.68f}, {0.0f, 2.0f}, orange}, // bottom left
    {{-0.05f, -0.68f}, {2.0f, 2.0f}, orange}, // bottom right
    {{-0.05f, 0.68f}, {2.0f, 0.0f}, white}, // top right
    {{-0.9f, 0.68f}, {0.0f, 0.0f}, white}, // top left
};

const Vertex rightQuad[] = {
    {{0.05f, -0.68f}, {0.0f, 2.0f}, orange},
    {{0.9f, -0.68f}, {2.0f, 2.0f}, orange},
    {{0.9f, 0.68f}, {2.0f, 0.0f}, white},
    {{0.05f, 0.68f}, {0.0f, 0.0f}, white},
};

const std::uint16_t quadIndices[] = {0, 1, 2, 0, 2, 3};

// A 16x16 RGBA8 image: four bytes per texel (red, green, blue, alpha, each
// 0..255), rows stored top to bottom. A 4x4 checkerboard whose light squares
// carry a gradient, red growing to the right and green growing downwards, so
// the top-left texel is blue and the bottom-right one is near white.
Texture makeCheckerTexture()
{
    constexpr auto size = 16;
    constexpr auto cell = 4;

    auto pixels = std::vector<std::uint8_t>(size * size * 4);

    for (auto y = 0; y < size; ++y)
    {
        for (auto x = 0; x < size; ++x)
        {
            auto light = (x / cell + y / cell) % 2 == 0;
            auto shade = light ? 1.0f : 0.3f;

            auto* texel = &pixels[(y * size + x) * 4];
            texel[0] = static_cast<std::uint8_t>(255.0f * shade * x / (size - 1));
            texel[1] = static_cast<std::uint8_t>(255.0f * shade * y / (size - 1));
            texel[2] = static_cast<std::uint8_t>(255.0f * shade * 0.8f);
            texel[3] = 255;
        }
    }

    auto descriptor = TextureDescriptor {};
    descriptor.width = size;
    descriptor.height = size;
    descriptor.format = TextureFormat::RGBA8Unorm;

    // Copies the pixels into GPU memory; the vector can go away afterwards.
    return Device::shared().makeTexture(descriptor, pixels.data());
}

struct TexturedShader final : ShaderProgram
{
    // How the texture is filtered and wrapped is baked into the shader, so it
    // has to be set before compile() records it.
    explicit TexturedShader(TextureSampling sampling)
    {
        image.sampling = sampling;
        compile();
    }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto uv = varying(vertexInput(&Vertex::uv));
        auto tint = varying(vertexInput(&Vertex::tint));

        setPosition(float4(position, 0.0f, 1.0f));

        // sample() asks the sampler for the colour at this UV. It returns a
        // Float4 (r, g, b, a) in 0..1, whatever the bytes were stored as.
        auto texel = sample(image, uv);

        // Texture times colour: white leaves the texture as it is, orange
        // keeps its red and removes some of its green and blue.
        setFragment(float4(texel.xyz() * tint, texel.w()));
    }

    // A texture is a uniform too: one image bound for the whole draw.
    Uniform<Texture2D> image;

    EACP_SHADER(image)
};
} // namespace

struct TexturesView final : GPUView
{
    TexturesView()
        : checker(makeCheckerTexture())
    {
        nearest.setVertices(leftQuad);
        nearest.setIndices(quadIndices);
        nearest.prepare(sampleCount());
        nearest.image = checker;

        linear.setVertices(rightQuad);
        linear.setIndices(quadIndices);
        linear.prepare(sampleCount());
        linear.image = checker;
    }

    void render(Frame& frame) override
    {
        auto pass = frame.beginPass({Graphics::Color {0.10f, 0.10f, 0.12f}});
        pass.draw(nearest);
        pass.draw(linear);
    }

    // The shaders only point at the texture, so it is declared first and
    // therefore outlives them.
    Texture checker;

    TexturedShader nearest {{TextureFilter::Nearest, TextureAddressMode::Repeat}};
    TexturedShader linear {{TextureFilter::Linear, TextureAddressMode::Clamp}};
};

int main()
{
    return Graphics::runWindowedApp<TexturesView>();
}
