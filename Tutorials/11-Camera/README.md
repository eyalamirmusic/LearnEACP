# 11. Camera

Every scene so far was watched from one fixed spot, typed into a
`Mat4::lookAt` call. In this chapter the viewer gets to move. You will build a
small `Camera` class of your own, the kind of **fly camera** you find in 3D
editors and first-person games: drag with the mouse to look around, scroll to
zoom, and walk through the scene with W, A, S and D. Along the way you will see
what `lookAt` actually computes, how two angles, **yaw** and **pitch**, describe
any direction you could look in, why pitch has to stop just short of straight
up, and how keyboard movement stays the same speed on a 60 Hz and a 120 Hz
display.

There is nothing new on the GPU side. A camera is pure CPU maths: it produces
the view and projection matrices, and the shader receives them as uniforms
exactly as it did in chapters 08 and 09.

## What a camera is

The GPU has no camera. As chapter 08 (Coordinate Systems) put it, the **view
matrix** moves the whole world so that the viewer ends up at the origin,
looking down -z. A "camera", then, is just a convenient set of numbers from
which we build that matrix each frame. A good set is:

- a **position**: where the eye is, in world space;
- a **direction**: which way it is looking;
- an **up** vector: which way is the top of the picture;
- a **field of view**: how wide the lens is, which goes into the projection
  matrix rather than the view matrix.

The rest of the chapter is about where those numbers come from (the mouse, the
keyboard, the clock) and how they turn into matrices.

## Inside lookAt

`Mat4::lookAt(eye, target, up)` has been doing this job since chapter 08. It
is worth seeing what it does, because the camera class leans on the same
three vectors.

From the eye and the target it works out the camera's own set of axes:

```
  forward = normalize(target - eye)      the way the camera looks
  right   = normalize(forward x up)      to the camera's right
  camUp   = right x forward              the camera's true up
```

The `up` you pass in only has to point *roughly* upward: the cross products
throw away any part of it that leans along `forward`, and `camUp` comes out
exactly perpendicular to both other axes. Three mutually perpendicular unit
vectors are a **basis**: a little coordinate system attached to the camera.

```
              camUp
               ^
               |   forward
               |  /
               | /
         eye   +--------> right
```

The view matrix is then two steps. First translate by `-eye`, so the camera
sits at the origin. Then rotate so that `right` lines up with +x, `camUp` with
+y, and `forward` with **-z** (the camera looks down -z in a right-handed view
space). Because the basis vectors are orthonormal, that rotation is simply
their components written as the rows of the matrix:

```
  | right.x     right.y     right.z    0 |   | 1 0 0 -eye.x |
  | camUp.x     camUp.y     camUp.z    0 | * | 0 1 0 -eye.y |
  | -forward.x  -forward.y  -forward.z 0 |   | 0 0 1 -eye.z |
  | 0           0           0          1 |   | 0 0 0  1     |
```

That is all `lookAt` is. Notice what goes wrong if `forward` is parallel to
`up`: their cross product is the zero vector, `right` cannot be normalised,
and the matrix falls apart. Keep that in mind for the section on pitch.

A fly camera does not have a target, it has a direction. That is no problem:
any point straight ahead of the eye will do as a target, so we ask for
`lookAt(position, position + front, worldUp)`.

## Describing a direction with yaw and pitch

To steer the camera with a mouse we need a direction we can change in two
independent ways: turning left and right, and tilting up and down. Those two
angles have names borrowed from aircraft:

- **yaw** is the turn about the world's vertical (y) axis: looking around the
  horizon.
- **pitch** is the tilt up or down from the horizon.

(A third angle, **roll**, tilts the horizon itself, as an aircraft banking. A
fly camera leaves it out, which is why it can always use the world's y axis as
its up vector.)

Turning two angles into a direction is a little trigonometry. Picture pitch
first, seen from the side: a unit vector tilted up by `pitch` has a height of
`sin(pitch)` and reaches `cos(pitch)` across the ground. Then yaw, seen from
above, swings that ground-level part around the circle: `cos(yaw)` of it along
x, `sin(yaw)` along z.

```
  from the side                     from above (looking down on the xz plane)

        front                               z
        /|                                  ^
       / |  sin(pitch)                      |    front (ground part)
      /  |                                  |   /
     /p  |                                  |  / cos(pitch)
    +----+                                  | /
     cos(pitch)                             |/ yaw
                                            +------------> x
```

```
  front.x = cos(yaw) * cos(pitch)
  front.y = sin(pitch)
  front.z = sin(yaw) * cos(pitch)
```

With this formula, yaw 0 looks along +x. We want the camera to start out
looking into the screen, along -z, as every chapter so far has done, so its
yaw starts at **-90 degrees** (`cos = 0`, `sin = -1`).

Going the other way, from a direction back to the angles, is just as short and
is what the automatic camera in this chapter uses to aim at a point:

```
  yaw   = atan2(front.z, front.x)
  pitch = asin(front.y)
```

### Why pitch is clamped

Let pitch reach 90 degrees and `front` points straight up, parallel to the
world's up vector. That is exactly the case where `lookAt` divides by zero.
Go past it, and the camera flips over: the picture turns upside down and the
mouse controls reverse. So the camera clamps pitch to the range -89 to 89
degrees. You can still look almost straight up or down, and nothing ever
breaks. Yaw needs no limit: turning round and round is just angles that keep
growing, and `cos` and `sin` do not mind.

## Looking around with the mouse

Mouse-look is the mapping from "the pointer moved this far" to "the angles
changed this much". The pointer's movement since the last event, multiplied by
a **sensitivity** in degrees per point, goes straight into yaw and pitch.

EACP delivers this through the `Graphics::View` that `GPUView` is built on.
A view only receives mouse events once it says it handles them, with
`setHandlesMouseEvents(true)`. Then three virtual functions matter here:

- `mouseDown` when a button goes down, where we remember where the pointer was;
- `mouseDragged` while it moves with the button held, where we turn by how far
  it moved since the last event;
- `mouseWheel` for the scroll wheel or a two-finger trackpad scroll.

Event positions are in **points** from the view's top-left corner, so x grows
to the right and **y grows downward**. Dragging upward is a negative change in
y, and we want that to look *up*, which is a positive change in pitch: hence
the minus sign on the y term.

This is "drag to look", which suits a tutorial program in a window. A game
usually hides and locks the pointer instead, so that every mouse movement turns
the camera without any button held; EACP's `Maze` example does that with
`Window::setMouseLocked`.

## Zooming: the field of view

A real camera zooms by changing its lens, and ours can do the same. The
**field of view** from chapter 08 is how wide an angle the projection takes
in. Make it smaller and a smaller part of the scene fills the window, so
everything looks bigger: a zoom in. Make it larger and you get a wide-angle
view.

```
   wide field of view (zoomed out)      narrow field of view (zoomed in)

          \           /                          \     /
           \         /                            \   /
            \       /                              \ /
             \     /                                +
              \   /                                eye
               \ /
                +
               eye
```

The scroll wheel changes the field of view, clamped between 15 and 75 degrees.
Zooming is not the same as moving closer: nothing changes position, so things
never slide past each other, the picture is only magnified. (Try both and
watch the near cubes against the far ones: walking forward changes what hides
what, zooming does not.)

Wheel events need one detail. A notched mouse wheel reports its movement in
*lines*, usually one per click; a trackpad reports *points*, many small steps
per gesture. `event.preciseScrolling` says which one arrived, and the camera
uses a different scale for each so that both zoom at a similar rate.

## Moving: frame-rate-independent speed

The keyboard moves the camera in its *own* frame of reference: W walks along
`front`, A and D step along `right`, and Q and E move straight down and up
along the world's y axis.

Keys arrive as events: `keyDown` once when a key goes down (and repeated while
it is held, at whatever rate the operating system's key repeat is set to),
`keyUp` when it is released. Moving on each `keyDown` would make the speed
depend on that repeat rate, and would stutter. Instead, the view only records
*which keys are held*, and `update()` reads that set every frame and moves
accordingly.

How far to move in one frame? Chapter 04 (Uniforms) answered this for
animation: never "a fixed amount per frame", because a 120 Hz display would
then run twice as fast as a 60 Hz one. Multiply a **speed** in units per second
by the frame's `FrameTime::delta`, the seconds since the last frame:

```
  position += direction * speed * delta
```

At 60 Hz each step is 1/60 of the speed, at 120 Hz it is half that, and either
way the camera covers `speed` units every second. The direction is normalised
first, so holding W and D together moves at the same speed as holding W alone,
rather than about 1.4 times as fast.

Mouse look does *not* use `delta`. The mouse reports a distance, not a rate:
moving the hand an inch should turn the camera the same amount however long
it took, so the angle change is proportional to the distance alone.

## Walking through the code

The cube mesh is chapter 09's `makeCube`, with positions and normals. One
shader draws every object in the scene. It lights each surface with ambient
plus diffuse light from a distant sun (a **directional light**: the same
direction everywhere, so there is no light position, only `lightDirection`).
Two small extras make the camera's motion easy to read. A `checker` uniform
turns on a world-space checkerboard for the floor, so you can see yourself
moving across it. And the colour fades into the sky with distance from the
camera, a simple **fog** that hides the floor's far edge:

```cpp
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
```

`smoothstep(15, 45, distance)` is 0 up to 15 units away, 1 beyond 45, and
blends smoothly in between; `mix` uses it to slide from the lit colour to the
sky.

The heart of the chapter is the `Camera`. It stores five numbers, and derives
everything else from them on demand:

```cpp
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
```

`front()` is the yaw-and-pitch formula from above. The angles are stored in
degrees, which are easier to read and to clamp, and converted with
`Maths::radians` for `std::cos` and `std::sin`. `right()` is the same cross
product `lookAt` uses, and since `worldUp` is vertical, `right` always stays
level with the ground: strafing never drifts up or down. The two matrix
functions are the ones you have been writing by hand since chapter 08, now
fed from the camera's state.

Then the operations the input calls:

```cpp
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
```

`move` moves *along* `front()`, so pressing W while looking down flies you
down towards the floor, like a spectator camera. (Exercise 2 turns it into a
walking camera that stays at one height.) `face` is the inverse formula, from a
direction back to yaw and pitch.

The scene is a table of ten cubes at different positions, sizes and angles,
spread out in depth so that moving the camera visibly changes which cube hides
which. The floor is one more cube, flattened into a wide slab.

The view turns on mouse handling and the same depth-tested, culled pipeline as
chapter 09. The mouse functions are a few lines each:

```cpp
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
```

A positive `delta.y` is a scroll that would move a document's content down
(EACP has already applied the user's natural-scrolling preference), which we
take as "zoom in".

The keyboard keeps a set of held keys. A view that overrides `keyDown` and
`keyUp` keeps every key it is given, so for any key it does not use (or one
pressed with Command) it calls `passKeyOn()`, which lets the key go on to the
window and the application: without that, Cmd+Q would stop quitting the
program.

```cpp
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
```

`event.keyCode` identifies the physical key, compared against
`Graphics::KeyCode::W` and friends, so the controls stay in the same place on
any keyboard layout. A held key sends repeated `keyDown`s; inserting into a
set twice is harmless.

`update()` runs once per frame. Until the user has touched anything the
camera flies itself, on a slow circle around the scene that bobs up and down,
aimed at the scene's centre with `face`. This is why `face` sets yaw and
pitch rather than building a matrix: the moment you take over, the camera
already *is* a yaw-and-pitch camera looking where the tour was looking, and
carries on from there without a jump.

```cpp
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
```

`axis` turns a pair of opposite keys into -1, 0 or 1, so holding both W and S
cancels out. The check before `move` matters: with no key held the direction
would be the zero vector, and although `Maths::normalize` safely returns zero
for it, there is no reason to move at all.

`render` asks the camera for its matrices and draws eleven cubes with one
shader:

```cpp
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
```

In chapter 09 the lamp had a shader object of its own. Here a single
`SceneShader` draws all eleven objects: `pass.draw` records the uniform values
as they are at that moment, so changing `model` and drawing again draws the
next cube without disturbing the one before. (Drawing many copies of one mesh
this way is one draw call each; chapter 18 (Instancing) shows how to draw them
all in one.) The floor's model matrix is a strongly non-uniform scale, so it
goes through chapter 09's normal matrix as a matter of course (for a cube's
axis-aligned faces it happens not to matter, as chapter 09 noted).

## What you should see

A field of coloured cubes on a grey checkerboard that fades into a pale blue
sky. The camera drifts slowly around the scene on its own, rising and falling
a little. Click or drag in the window and the tour stops where it is: drag
to look around, scroll to zoom in and out, hold W, A, S, D to walk and Q, E to
sink and rise. Turning past straight up or down is not possible; the view
stops just before it.

## Try it yourself

1. Hold Shift to move faster. *Hint: the `KeyEvent` and `MouseEvent` both
   carry `modifiers`, but on macOS Shift on its own sends no key event; read
   `Graphics::Keyboard::isShiftPressed()` in `update()` instead.*
2. Turn the fly camera into a walking camera: W and S should move you across
   the ground without changing height, however far up or down you look.
   *Hint: build the forward direction from yaw alone, with a y of 0.*
3. Make the scroll wheel move the camera forward and back instead of changing
   the field of view, and compare the two. *Hint: watch how near and far cubes
   line up; only moving changes the parallax.*
4. Write an **orbit camera**, as 3D modelling tools use: it always looks at a
   fixed target, dragging moves the eye around the target on a sphere, and the
   wheel changes the distance. *Hint: the eye is
   `target - front() * distance`, and `face` is no longer needed.*
5. Remove the pitch clamp and drag straight up past the vertical. What happens
   to the picture, and to the mouse controls? *Hint: re-read "Inside lookAt"
   and think about what `forward x up` is at that moment.*
6. Make the tour resume after a few seconds without input. *Hint: record the
   `clock` time of the last input; to blend smoothly from where the user left
   the camera, interpolate the position rather than jumping.*

## Further reading

- EACP's `Graphics::View` header, `Lib/eacp/Graphics/View/View.h`: every mouse,
  wheel, pinch and key callback a view can override, and what each
  `MouseEvent` field means, including `rawDelta` for pointer movement without
  the system's acceleration curve.
- `Apps/GPU/Maze` in EACP: a first-person camera with locked-pointer mouse
  look, polled keyboard input through `GameInput`, and collision with walls.
- EACP GPU README, [The CPU types a shader value is fed from](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#the-cpu-types-a-shader-value-is-fed-from): `Mat4::lookAt` and `perspective`, right-handed with 0..1 depth.
- learnopengl.com, [Camera](https://learnopengl.com/Getting-started/Camera).
- Song Ho Ahn, [OpenGL lookAt](https://www.songho.ca/opengl/gl_camera.html), for the derivation of the view matrix.

Previous: [10. Compute](../10-Compute/README.md) | Next: [12. Lighting Maps](../12-LightingMaps/README.md)
