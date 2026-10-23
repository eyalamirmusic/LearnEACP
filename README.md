# LearnEACP

LearnEACP is a tutorial series for learning graphics programming with
[EACP](https://github.com/eyalamirmusic/eacp) and its C++ shader EDSL, in the
spirit of [learnopengl.com](https://learnopengl.com). Shaders are written as
ordinary C++ structs instead of strings in a separate shading language, and the
same code runs on Metal, Direct3D 12 and Vulkan.

Each chapter is a small, complete program with a written walkthrough. Start at
chapter 1 and work forward: every chapter builds on the ideas of the ones
before it. All the chapters can also be explored in one app, the Tour, which
shows each chapter's code next to its result.

## Prerequisites

- CMake 3.31 or newer
- A C++20 toolchain: Xcode / Apple Clang on macOS, Visual Studio 2022 (MSVC or
  clang-cl) on Windows, or a recent GCC or Clang on Linux
- [Ninja](https://ninja-build.org) (recommended)
- Git, used to download EACP and its dependencies on the first configure

macOS, Windows and Linux are supported, as EACP is. A GPU with Metal,
Direct3D 12 or Vulkan support is needed to run the chapters.

## Building

```sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The first configure downloads EACP, and the first build compiles it, so it
takes a while. Later builds only recompile what changed.

Tip: add `-DCPM_SOURCE_CACHE=$HOME/.cache/CPM` to the configure command to keep
downloaded dependencies in one shared place, so a fresh build directory (or
another project using them) does not download them again. Cached copies are
never refreshed, though: EACP's dependencies follow their `main` branches, so
if a build fails with a missing header in one of them, delete that
dependency's folder under `~/.cache/CPM` (or the whole cache) and configure
again. EACP itself is always downloaded fresh.

To build a single chapter, name its target:

```sh
cmake --build build --target HelloTriangle
```

## Running a chapter

Every chapter builds into `build/Tutorials/<chapter directory>/`.

On macOS each chapter is an app bundle. Open it from Finder, or from a terminal:

```sh
open build/Tutorials/01-HelloWindow/HelloWindow.app
# or, to see its console output:
build/Tutorials/01-HelloWindow/HelloWindow.app/Contents/MacOS/HelloWindow
```

On Windows and Linux run the executable directly, for example
`build/Tutorials/01-HelloWindow/HelloWindow` (with `.exe` on Windows).

Chapter 10 is the exception: it is a console program that opens no window,
prints its results and exits. Run it from a terminal to see what it prints:

```sh
build/Tutorials/10-Compute/Compute.app/Contents/MacOS/Compute   # macOS
build/Tutorials/10-Compute/Compute                              # Windows and Linux
```

## Curriculum

| # | Chapter | What you will learn |
| --- | --- | --- |
| 1 | [Hello Window](Tutorials/01-HelloWindow) | A GPUView that clears the screen each frame: the render loop, the swapchain, a frame. |
| 2 | [Hello Triangle](Tutorials/02-HelloTriangle) | The first ShaderProgram, a vertex struct, setPosition and setFragment: the graphics pipeline and clip space. |
| 3 | [Interpolation](Tutorials/03-Interpolation) | Per-vertex colours carried through varyings, and a quad built from two triangles with an index buffer. |
| 4 | [Uniforms](Tutorials/04-Uniforms) | Uniform<Float> time, continuous rendering, update(FrameTime) and frame-rate-independent animation. |
| 5 | [Fragment Playground](Tutorials/05-FragmentPlayground) | A full-screen quad, UVs, resolution and aspect, gradients and distance-field shapes (Shadertoy-style). |
| 6 | [Textures](Tutorials/06-Textures) | A procedurally generated texture, Uniform<Texture2D>, sample(), UV mapping, and nearest/linear filtering with repeat/clamp wrapping. |
| 7 | [Transformations](Tutorials/07-Transformations) | Maths::Mat4 translation, rotation and scale, Uniform<Float4x4> and multiplication order. |
| 8 | [Coordinate Systems](Tutorials/08-CoordinateSystems) | Model, view and projection, perspective, depth testing and back-face culling on a spinning 3D cube. |
| 9 | [Lighting](Tutorials/09-Lighting) | Normals, the normal matrix, and per-fragment ambient, diffuse and specular (Phong) lighting from an orbiting point light. |
| 10 | [Compute](Tutorials/10-Compute) | A ComputeProgram, threadId, buffers, dispatch and read-back, plus the CPU executor, in a console program with no window. |
| 11 | [Camera](Tutorials/11-Camera) | A fly camera: lookAt, yaw and pitch, mouse-drag look, scroll-wheel zoom and WASD movement scaled by the frame delta. |
| 12 | [Lighting Maps](Tutorials/12-LightingMaps) | Material and light properties as grouped uniforms, and procedural diffuse and specular maps on a steel-framed wooden crate. |
| 13 | [Light Casters](Tutorials/13-LightCasters) | Directional, point and spot lights as EDSL helper functions summed in one shader, with the point lights in a fragment-stage storage buffer. |
| 14 | [Stencil Testing](Tutorials/14-StencilTesting) | The stencil buffer, its tests, operations, reference value and write mask: outlining objects with a scaled-up second pass. |
| 15 | [Blending](Tutorials/15-Blending) | Discarding transparent texels, the blend equation as a BlendState, depth writes off for glass, and sorting transparent windows back to front. |
| 16 | [Framebuffers](Tutorials/16-Framebuffers) | Rendering the scene into an offscreen colour and depth texture, then post-processing it on a full-screen quad with inversion, grayscale and kernels. |
| 17 | [Cubemaps](Tutorials/17-Cubemaps) | A procedural cube map, Uniform<TextureCube>, a skybox drawn at the far plane, and environment-mapped reflection and refraction. |
| 18 | [Instancing](Tutorials/18-Instancing) | instanceInput, setInstances and drawInstanced: a 12,000-rock asteroid ring in one draw call, timed against one draw per rock. |
| 19 | [Anti-Aliasing](Tutorials/19-AntiAliasing) | Aliasing, supersampling and multisampling, the window's built-in MSAA, and a multisampled texture target resolved before it is read. |
| 20 | [Shadow Mapping](Tutorials/20-ShadowMapping) | A depth-only pass from an orthographic sun, light-space lookup, slope-scaled bias against acne, out-of-frustum handling and 3x3 PCF. |
| 21 | [Normal Mapping](Tutorials/21-NormalMapping) | A procedural tangent-space normal map, tangents and bitangents from UVs, the TBN matrix, and EACP's green-down convention. |
| 22 | [HDR and Bloom](Tutorials/22-HDRBloom) | A half-float scene target, exposure and Reinhard tone mapping, gamma correction, and bloom from a separable Gaussian ping-pong blur. |
| 23 | [Deferred Shading](Tutorials/23-DeferredShading) | A G-buffer filled by lighting-free geometry passes, then one full-screen pass that loops over 96 moving lights with light-volume radii. |

## Further reading

- [EACP on GitHub](https://github.com/eyalamirmusic/eacp)
- [EACP's GPU module and shader EDSL](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md)
