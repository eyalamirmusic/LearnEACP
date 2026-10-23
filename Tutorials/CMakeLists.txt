set(CMAKE_FOLDER Tutorials)

# A GPUView that clears the screen each frame: the render loop, the swapchain, a frame.
add_subdirectory(01-HelloWindow)

# The first ShaderProgram, a vertex struct, setPosition and setFragment: the graphics pipeline and clip space.
add_subdirectory(02-HelloTriangle)

# Per-vertex colours carried through varyings, and a quad built from two triangles with an index buffer.
add_subdirectory(03-Interpolation)

# Uniform<Float> time, continuous rendering, update(FrameTime) and frame-rate-independent animation.
add_subdirectory(04-Uniforms)

# A full-screen quad, UVs, resolution and aspect, gradients and distance-field shapes (Shadertoy-style).
add_subdirectory(05-FragmentPlayground)

# A procedurally generated texture, Uniform<Texture2D>, sample(), UV mapping, and nearest/linear filtering with repeat/clamp wrapping.
add_subdirectory(06-Textures)

# Maths::Mat4 translation, rotation and scale, Uniform<Float4x4> and multiplication order.
add_subdirectory(07-Transformations)

# Model, view and projection, perspective, depth testing and back-face culling on a spinning 3D cube.
add_subdirectory(08-CoordinateSystems)

# Normals, the normal matrix, and per-fragment ambient, diffuse and specular (Phong) lighting from an orbiting point light.
add_subdirectory(09-Lighting)

# A ComputeProgram, threadId, buffers, dispatch and read-back, plus the CPU executor, in a console program with no window.
add_subdirectory(10-Compute)

# A fly camera: lookAt, yaw and pitch, mouse-drag look, scroll-wheel zoom and WASD movement scaled by the frame delta.
add_subdirectory(11-Camera)

# Material and light properties as grouped uniforms, and procedural diffuse and specular maps on a steel-framed wooden crate.
add_subdirectory(12-LightingMaps)

# Directional, point and spot lights as EDSL helper functions summed in one shader, with the point lights in a fragment-stage storage buffer.
add_subdirectory(13-LightCasters)

# The stencil buffer, its tests, operations, reference value and write mask: outlining objects with a scaled-up second pass.
add_subdirectory(14-StencilTesting)

# Discarding transparent texels, the blend equation as a BlendState, depth writes off for glass, and sorting transparent windows back to front.
add_subdirectory(15-Blending)

# Rendering the scene into an offscreen colour and depth texture, then post-processing it on a full-screen quad with inversion, grayscale and kernels.
add_subdirectory(16-Framebuffers)

# A procedural cube map, Uniform<TextureCube>, a skybox drawn at the far plane, and environment-mapped reflection and refraction.
add_subdirectory(17-Cubemaps)

# instanceInput, setInstances and drawInstanced: a 12,000-rock asteroid ring in one draw call, timed against one draw per rock.
add_subdirectory(18-Instancing)

# Aliasing, supersampling and multisampling, the window's built-in MSAA, and a multisampled texture target resolved before it is read.
add_subdirectory(19-AntiAliasing)

# A depth-only pass from an orthographic sun, light-space lookup, slope-scaled bias against acne, out-of-frustum handling and 3x3 PCF.
add_subdirectory(20-ShadowMapping)

# A procedural tangent-space normal map, tangents and bitangents from UVs, the TBN matrix, and EACP's green-down convention.
add_subdirectory(21-NormalMapping)

# A half-float scene target, exposure and Reinhard tone mapping, gamma correction, and bloom from a separable Gaussian ping-pong blur.
add_subdirectory(22-HDRBloom)

# A G-buffer filled by lighting-free geometry passes, then one full-screen pass that loops over 96 moving lights with light-volume radii.
add_subdirectory(23-DeferredShading)
