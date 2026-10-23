// 11. Camera: a fly-through camera of our own, built on the CPU from a
// position, a yaw and a pitch. Drag with the mouse to look around, scroll to
// zoom, and move with W/A/S/D (Q and E go down and up). Until you touch
// anything, the camera circles the scene on its own.
#include <eacp/GPU/GPU.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

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

// The unit cube with per-face normals from chapter 09 (Lighting).
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

// One shader for every object in the scene: ambient plus diffuse light from a
// distant sun, and an optional world-space checkerboard so the floor shows
// how fast you are moving over it.
struct SceneShader final : ShaderProgram
{
    SceneShader() { compile(); }

    void define() override
    {
        auto position = vertexInput(&Vertex::position);
        auto normal = vertexInput(&Vertex::normal);

        auto worldPosition = (model * float4(position, 1.0f)).xyz();
        auto worldNormal = (normalMatrix * float4(normal, 0.0f)).xyz();

        setPosition(viewProjection * float4(worldPosition, 1.0f));

        auto fragmentPosition = varying(worldPosition);
        auto n = normalize(varying(worldNormal));

        // Alternate 1-unit tiles: floor(x) + floor(z) is even on one colour
        // and odd on the other, and fract(sum / 2) * 2 turns that into 0 or 1.
        auto cells = floor(fragmentPosition.x()) + floor(fragmentPosition.z());
        auto tile = fract(cells * 0.5f) * 2.0f;
        auto pattern = 1.0f - checker * 0.2f * tile;

        auto diffuse = max(dot(n, -lightDirection), 0.0f);
        auto lit = (0.35f + 0.65f * diffuse) * pattern * objectColor;

        // Distance fog: fade towards the sky colour with distance from the
        // camera, so the floor melts into the horizon instead of ending.
        auto distance = length(fragmentPosition - cameraPosition);
        auto fog = smoothstep(15.0f, 45.0f, distance);

        setFragment(float4(mix(lit, skyColor, fog), 1.0f));
    }

    Uniform<Float4x4> model;
    Uniform<Float4x4> normalMatrix;
    Uniform<Float4x4> viewProjection;
    Uniform<Float3> lightDirection;
    Uniform<Float3> objectColor;
    Uniform<Float> checker;
    Uniform<Float3> cameraPosition;
    Uniform<Float3> skyColor;

    EACP_SHADER(model,
                normalMatrix,
                viewProjection,
                lightDirection,
                objectColor,
                checker,
                cameraPosition,
                skyColor)
};

// A fly camera: where it is, which way it faces (as two angles), and how wide
// it sees. Everything else - the direction vectors, the view and projection
// matrices - is derived from these few numbers on the CPU.
struct Camera
{
    // The direction the camera faces. Yaw turns it around the world's y axis,
    // pitch tilts it up and down. Yaw 0 faces +x; yaw -90 faces -z, the way
    // into the screen in a right-handed world.
    Vec3 front() const
    {
        auto y = radians(yaw);
        auto p = radians(pitch);
        return {std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)};
    }

    // Level with the ground, perpendicular to where the camera faces.
    Vec3 right() const { return normalize(cross(front(), worldUp)); }

    Mat4 viewMatrix() const
    {
        return Mat4::lookAt(position, position + front(), worldUp);
    }

    Mat4 projectionMatrix(float aspect) const
    {
        return Mat4::perspective(aspect, radians(fieldOfView), 0.1f, 100.0f);
    }

    // Mouse look. Pitch stops short of straight up or down: at exactly +-90
    // degrees front() is parallel to worldUp, their cross product is zero and
    // lookAt has no way to tell which way is right.
    void turn(float yawDegrees, float pitchDegrees)
    {
        yaw += yawDegrees;
        pitch = std::clamp(pitch + pitchDegrees, -89.0f, 89.0f);
    }

    // Zoom by narrowing or widening the lens, within sensible limits.
    void zoom(float amount)
    {
        fieldOfView = std::clamp(fieldOfView - amount, 15.0f, 75.0f);
    }

    // Moves in the camera's own frame: forward along front(), sideways along
    // right(), and straight up and down along the world's y axis. Each input
    // is -1, 0 or 1; the distance is speed times the time this frame took.
    void move(float forward, float sideways, float upward, float seconds)
    {
        auto direction = front() * forward + right() * sideways + worldUp * upward;
        position += normalize(direction) * (speed * seconds);
    }

    // Turns the camera to face a point: the inverse of front(), recovering
    // yaw and pitch from a direction.
    void face(const Vec3& target)
    {
        auto d = normalize(target - position);
        yaw = degrees(std::atan2(d.z, d.x));
        pitch = std::clamp(degrees(std::asin(d.y)), -89.0f, 89.0f);
    }

    static constexpr Vec3 worldUp {0.0f, 1.0f, 0.0f};

    Vec3 position {0.0f, 2.0f, 9.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fieldOfView = 50.0f;
    float speed = 3.0f; // units per second
};

struct Object
{
    Vec3 position;
    float size;
    float angle; // radians about y
    Vec3 color;
};

// A field of cubes at different distances and heights, so that moving the
// camera visibly changes what hides what. The ones at y = size / 2 rest on
// the floor; the others float.
const Object objects[] = {
    {{0.0f, 0.5f, 0.0f}, 1.0f, 0.0f, {0.95f, 0.55f, 0.30f}},
    {{2.6f, 1.6f, -3.0f}, 1.0f, 0.6f, {0.35f, 0.65f, 0.95f}},
    {{-2.8f, 0.6f, -1.4f}, 1.2f, 0.3f, {0.45f, 0.85f, 0.45f}},
    {{-1.4f, 2.4f, -4.6f}, 0.8f, 1.1f, {0.90f, 0.40f, 0.55f}},
    {{3.3f, 0.5f, 1.9f}, 1.0f, 0.9f, {0.95f, 0.85f, 0.35f}},
    {{-3.6f, 1.8f, 2.4f}, 0.9f, 0.4f, {0.55f, 0.45f, 0.90f}},
    {{1.0f, 0.75f, -6.6f}, 1.5f, 0.2f, {0.35f, 0.85f, 0.80f}},
    {{-0.6f, 0.4f, 3.6f}, 0.8f, 0.7f, {0.90f, 0.60f, 0.85f}},
    {{4.6f, 2.2f, -5.6f}, 1.1f, 1.3f, {0.95f, 0.45f, 0.35f}},
    {{-4.9f, 0.7f, -5.2f}, 1.4f, 0.5f, {0.70f, 0.80f, 0.40f}},
};

constexpr Vec3 floorColor {0.62f, 0.64f, 0.70f};
} // namespace

struct CameraView final : GPUView
{
    CameraView()
    {
        setDepth(true);
        setHandlesMouseEvents(true);

        auto pipeline = RenderPipelineDescriptor {};
        pipeline.sampleCount = sampleCount();
        pipeline.depth = true;
        pipeline.cullMode = CullMode::Back;

        shader.setVertices(cube.vertices);
        shader.setIndices(cube.indices.data(), (int) cube.indices.size());
        shader.prepare(pipeline);

        setContinuous(true);
    }

    // Any input hands the camera over to the user. The camera's yaw and pitch
    // were kept up to date while it flew itself, so it continues from exactly
    // where it was looking.
    void takeControl() { autopilot = false; }

    void mouseDown(const Graphics::MouseEvent& event) override
    {
        takeControl();
        lastMouse = event.pos;
    }

    // Drag right to turn right, drag up to look up. Positions are in points
    // from the view's top-left corner, so y grows downward and a drag upward
    // is a negative change in y.
    void mouseDragged(const Graphics::MouseEvent& event) override
    {
        auto dx = event.pos.x - lastMouse.x;
        auto dy = event.pos.y - lastMouse.y;
        lastMouse = event.pos;

        camera.turn(dx * lookSensitivity, -dy * lookSensitivity);
    }

    // A trackpad reports the scroll in points, a notched wheel in lines; they
    // need very different scales to zoom at a similar rate.
    void mouseWheel(const Graphics::MouseEvent& event) override
    {
        takeControl();
        auto step = event.preciseScrolling ? 0.1f : 2.0f;
        camera.zoom(event.delta.y * step);
    }

    // Key events arrive once when a key goes down and once when it comes up,
    // plus repeats while it is held. Movement wants to know whether a key is
    // held *now*, every frame, so we only remember which keys are down and let
    // update() act on them.
    void keyDown(const Graphics::KeyEvent& event) override
    {
        if (!isMovementKey(event) || event.modifiers.command)
        {
            // Not ours: hand it on, so Cmd+Q and friends still work.
            passKeyOn();
            return;
        }

        takeControl();
        heldKeys.insert(event.keyCode);
    }

    void keyUp(const Graphics::KeyEvent& event) override
    {
        if (!isMovementKey(event))
        {
            passKeyOn();
            return;
        }

        heldKeys.erase(event.keyCode);
    }

    void update(Threads::FrameTime time) override
    {
        auto seconds = static_cast<float>(time.delta);
        clock += seconds;

        if (autopilot)
        {
            // Circle the scene slowly, bobbing a little, always facing its
            // centre.
            auto angle = 0.12f * clock + 1.2f;
            auto height = 3.2f + 0.8f * std::sin(0.3f * clock);
            camera.position = {
                9.5f * std::cos(angle), height, 9.5f * std::sin(angle)};
            camera.face(sceneCentre);
            return;
        }

        using namespace Graphics;

        auto axis = [this](uint16_t positive, uint16_t negative)
        {
            return (isHeld(positive) ? 1.0f : 0.0f)
                   - (isHeld(negative) ? 1.0f : 0.0f);
        };

        auto forward = axis(KeyCode::W, KeyCode::S);
        auto sideways = axis(KeyCode::D, KeyCode::A);
        auto upward = axis(KeyCode::E, KeyCode::Q);

        if (forward != 0.0f || sideways != 0.0f || upward != 0.0f)
            camera.move(forward, sideways, upward, seconds);
    }

    void render(Frame& frame) override
    {
        auto bounds = getLocalBounds();
        auto aspect = bounds.h > 0.0f ? bounds.w / bounds.h : 1.0f;

        shader.viewProjection =
            camera.projectionMatrix(aspect) * camera.viewMatrix();
        shader.lightDirection = normalize(Vec3 {-0.4f, -1.0f, -0.6f});
        shader.cameraPosition = camera.position;
        shader.skyColor = Vec3 {sky.r, sky.g, sky.b};

        auto pass = frame.beginPass({sky});

        // The floor: the same cube, flattened into a wide slab whose top is at
        // y = 0.
        drawCube(pass,
                 Mat4::translation({0.0f, -0.05f, 0.0f})
                     * Mat4::scale({200.0f, 0.1f, 200.0f}),
                 floorColor,
                 1.0f);

        for (const auto& object: objects)
            drawCube(pass,
                     Mat4::translation(object.position)
                         * Mat4::rotationY(object.angle) * Mat4::scale(object.size),
                     object.color,
                     0.0f);
    }

    // One program draws every object: set its uniforms, draw, and repeat.
    // Each draw takes a copy of the uniform values as they are at that moment.
    void drawCube(RenderPass& pass, const Mat4& model, Vec3 color, float checker)
    {
        shader.model = model;
        shader.normalMatrix = model.inverted().transposed();
        shader.objectColor = color;
        shader.checker = checker;
        pass.draw(shader);
    }

    static bool isMovementKey(const Graphics::KeyEvent& event)
    {
        using namespace Graphics::KeyCode;

        switch (event.keyCode)
        {
            case W:
            case A:
            case S:
            case D:
            case Q:
            case E:
                return true;
            default:
                return false;
        }
    }

    bool isHeld(uint16_t keyCode) const { return heldKeys.contains(keyCode); }

    static constexpr float lookSensitivity = 0.15f; // degrees per point
    static constexpr Graphics::Color sky {0.55f, 0.70f, 0.85f};
    static constexpr Vec3 sceneCentre {0.0f, 1.0f, -1.5f};

    CubeMesh cube = makeCube();
    SceneShader shader;
    Camera camera;
    std::unordered_set<uint16_t> heldKeys;
    Graphics::Point lastMouse;
    bool autopilot = true;
    float clock = 0.0f;
};

int main()
{
    return Graphics::runWindowedApp<CameraView>();
}
