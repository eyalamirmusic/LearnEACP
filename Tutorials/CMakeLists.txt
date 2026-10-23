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
