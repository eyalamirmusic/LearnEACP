# 10. Compute

Up to now every shader you've written has been part of a picture: vertices go
in, the rasterizer turns triangles into fragments, and pixels come out. In
this last chapter we put the picture down. You'll write a *compute kernel*, a
function the GPU runs a hundred thousand times at once with no vertices, no
triangles and no window, and use it to transform an array of numbers. You'll
send it to the GPU, wait for it, read the answer back, and then run the very
same kernel on the CPU and check that both processors agree. By the end
you'll know what threads, threadgroups and grids are, why a buffer is the
only data path compute needs, what waiting for the GPU costs, and when all of
this is worth doing.

## What a compute shader is

Recall the render pipeline from the earlier chapters:

```
vertices --> [vertex stage] --> [rasterizer] --> [fragment stage] --> pixels
              runs per vertex    fixed hardware   runs per pixel
```

The fragment stage was already a hint of something bigger. In chapter 05 (Fragment Playground) you
wrote one function and the GPU ran it for every pixel of the screen, in
parallel, each invocation knowing only its own coordinates. That's the real
trick a GPU knows: run the same small function thousands of times at once.

A **compute shader** (or **kernel**) is that trick with everything else
removed:

```
                    [kernel] x N  -->  whatever the kernel writes
                runs once per thread
```

There's no vertex stage, no rasterizer, no blending, no render target. You
say "run this function N times", and the GPU starts N **threads**. Each
thread runs the whole function, gets a different number telling it which one
it is, and reads and writes ordinary memory. What the numbers *mean* is up to
you: pixels of an image, particles in a simulation, samples of audio, weights
in a neural network. Here they'll be elements of an array.

## Threads, threadgroups and the grid

GPUs don't schedule threads one at a time. They run them in **threadgroups**
(Metal's word; Direct3D and Vulkan call them thread groups and workgroups): a
fixed-size batch of threads that are launched together on one compute unit of
the GPU, and can share a small, fast scratch memory and synchronise with each
other. All the threadgroups of one dispatch together form the **grid**.

```
dispatch(kernel, 100000)

grid (1563 groups)
+-------------------+-------------------+-----+---------------------+
| group 0           | group 1           | ... | group 1562          |
| t0 t1 t2 ... t63  | t64 t65 ... t127  |     | t99968 ... t99999   |
|                   |                   |     | (+ 32 idle threads) |
+-------------------+-------------------+-----+---------------------+
         ^
         each t is one thread running define()'s body

threadId() = groupId() * 64 + localId()
```

Three numbers describe where a thread is:

- `localId()`: its position inside its group, 0 to 63.
- `groupId()`: which group it belongs to, 0 to 1562 here.
- `threadId()`: its position in the whole grid, 0 to 99,999.

For a 1D kernel EACP uses groups of 64 threads, a size that suits every GPU
it targets (2D kernels get 8x8 groups and 3D kernels 4x4x4; a kernel can ask
for its own shape, see Further reading). You ask for a number of *threads*,
and EACP rounds up to whole groups. 100,000 isn't a multiple of 64, so the
last group has 32 threads too many. EACP emits a bounds check for you, and
those extra threads simply exit without doing anything. That's why the
program picks such an awkward count on purpose.

For this chapter only `threadId()` matters, and it's exactly what an array
loop wants. Where the CPU would write

```
for (i = 0; i < N; ++i) out[i] = f(in[i]);
```

the GPU starts N threads and hands thread `i` the number `i`. The loop is
gone: the loop *variable* has become the thread's identity. Thread `i` reads
element `i`, writes element `i`, and never touches anybody else's, so no two
threads can step on each other. Problems that split into independent pieces
like this are called **embarrassingly parallel**, and they're what GPUs eat
for breakfast.

## Buffers: the compute data path

A render shader gets its inputs from vertex attributes and its output goes to
the screen. A kernel has neither, so where does data come from and go to?
**Buffers**. A buffer is nothing more than a block of GPU memory, a run of
bytes with an address, like a `std::vector<float>` that lives on the
graphics card. You've used them already without thinking about it: your
vertex arrays were uploaded into buffers.

A kernel declares the buffers it uses as members:

- `Uniform<InputBuffer>` is a buffer of floats the kernel reads, by index:
  `input[i]`.
- `Uniform<OutputBuffer>` is a buffer of floats the kernel writes:
  `write(output, i, value)`.

(There are `uint` versions too, `UIntInputBuffer` and `UIntOutputBuffer`, for
indices and counts.) The same `GPU::Buffer` can be one kernel's output and the
next kernel's input, or even a vertex buffer for a draw, without ever coming
back to the CPU. That's what makes compute powerful inside a renderer. This
chapter does the simplest possible round trip instead: fill a buffer from the
CPU, let the kernel transform it, copy the result back.

On Apple Silicon the CPU and GPU share the same physical memory, so "copy it
back" is cheap. On a desktop PC with a discrete graphics card the buffer may
live across a PCIe bus, and reading it back is a real transfer. Either way
your code is the same.

## Submitting, waiting and reading back

The GPU is a separate processor that runs **asynchronously**: when you tell
it to do something it doesn't do it right then. You *record* commands into a
**command buffer** (a to-do list), *submit* the list, and the CPU carries on
immediately while the GPU works through it on its own schedule.

```
CPU:  record --> submit --> (free to do other work) --> wait ...... read
                    |                                       ^
GPU:                +--> run 1563 groups ------------------>+ done
```

That's great for throughput, but it means the output buffer holds garbage
until the GPU says it's finished. If you read it too early you get whatever
was in that memory before. So before reading you must **wait**: block the CPU
thread until the GPU reports that this command buffer has completed.

Waiting has a cost, and it's usually not the kernel itself. It's the round
trip: the submission travels to the driver, the GPU has to be woken and
scheduled, the work runs, completion is signalled back, and only then can the
CPU copy the bytes out. That fixed overhead is typically tens to hundreds of
microseconds, and the CPU sits idle during all of it. In a renderer you
avoid it entirely by keeping results on the GPU and feeding them to the next
draw. Here, where the whole point is to print the numbers, we pay it once.

## The same kernel on the CPU

EACP can also run a kernel without any GPU at all. `CpuCompute::Executor`
takes the kernel object you wrote, walks the same recorded graph that becomes
Metal or HLSL, and interprets it on the calling CPU thread over plain C++
arrays. There's only ever one source for the kernel, so the two versions
can't drift apart. That's useful for:

- **Testing.** A second, independent implementation of every kernel, so you
  can check GPU results against it, as this program does, even on a CI
  machine with no GPU.
- **Machines without a GPU**, such as a Linux server with no Vulkan driver,
  where the GPU path simply can't run.
- **Realtime audio threads.** An audio callback must return within a few
  milliseconds, every time, and must never allocate memory, take locks or
  wait on another processor. A GPU round trip breaks all of those rules. The
  executor does all its allocation when it's built, and after that a
  dispatch allocates nothing and runs synchronously, so a filter written as
  a kernel can run inside the callback.

It isn't a replacement for the GPU's throughput. It's the same answer,
computed somewhere else.

## Walking through the code

The whole program lives in [`Main.cpp`](Main.cpp). There's no `GPUView` this
time, and no window: a compute job has nothing to draw, so the program is a
console app that prints and exits. `Device::shared()` doesn't need a window,
it just asks the system for the default GPU.

### The kernel

```cpp
// A compute kernel. Where a ShaderProgram describes a vertex stage and a
// fragment stage, a ComputeProgram describes one function that the GPU runs
// once per thread. There are no vertices and no rasterizer: you say how many
// threads you want, and each one runs define()'s body with its own id.
struct SquareKernel final : ComputeProgram
{
    SquareKernel() { compile(); }

    // Runs once, on the CPU, inside compile(): like a shader's define(), it
    // records a graph that is then emitted as Metal, HLSL or GLSL.
    void define() override
    {
        // This thread's index in the whole grid: 0, 1, 2, ... count - 1. It
        // is the natural array index, so thread i owns element i.
        auto i = threadId();

        // input[i] reads one float from the input buffer.
        auto x = input[i];

        // write(buffer, index, value) is how a kernel stores its result. Every
        // thread writes a different element, so no two threads ever collide.
        write(output, i, x * x + offset);
    }

    // Storage buffers: plain GPU memory that the kernel reads or writes.
    Uniform<InputBuffer> input;
    Uniform<OutputBuffer> output;

    // An ordinary uniform, the same for every thread, as in a render shader.
    Uniform<Float> offset;

    EACP_SHADER(input, output, offset)
};
```

This should look very familiar. A `ComputeProgram` is shaped exactly like
the `ShaderProgram`s you've been writing: members for its inputs, a
`define()` that records the body, `compile()` in the constructor, and
`EACP_SHADER(...)` listing the members so EACP can find them.

Just as before, `define()` doesn't compute anything when it runs. It runs
once, on the CPU, and `i`, `x` and `x * x + offset` are handles that record
a graph of operations. The graph is then emitted as Metal Shading Language on
macOS (HLSL on Windows, GLSL on Linux), and *that* code is what runs once per
thread on the GPU.

What's different is what's missing. There's no `vertexInput`, no
`setPosition`, no `setFragment`. A kernel's only way of producing anything is
`write`. And `threadId()` takes the place of the vertex the vertex stage got
or the pixel coordinate the fragment stage got: it's the one thing that
makes this thread's work different from every other thread's.

`offset` is a `Uniform<Float>`, exactly like the uniforms of chapter 04 (Uniforms): one
value, the same for all hundred thousand threads.

### Buffers, prepare and dispatch

```cpp
    // Two buffers in GPU memory: one filled from our array, one left empty
    // for the kernel to write into.
    auto inputBuffer = device.makeBuffer(input.data(), bytes, BufferUsage::Storage);
    auto outputBuffer = device.makeBuffer(bytes);

    kernel.input = inputBuffer;
    kernel.output = outputBuffer;

    // Compiles the emitted source into a compute pipeline on this device.
    kernel.prepare(device);
```

`makeBuffer(data, bytes, usage)` allocates GPU memory and copies our
`std::vector` into it. `BufferUsage::Storage` says a kernel will access it
(the default for `makeBuffer` with data is `Vertex`). `makeBuffer(bytes)`
allocates without initialising, and already defaults to `Storage`. Assigning
a buffer to a member binds it: when the kernel runs, `input[i]` reads from
`inputBuffer`.

`prepare()` plays the same role it did for your shaders. It hands the
emitted source to the driver, which compiles it to machine code and wraps it
in a **compute pipeline**. A typo that compiled fine as C++ would surface
here, at runtime.

```cpp
    // A command buffer outside any frame: we are not drawing, just computing.
    auto commands = device.makeCommandBuffer();

    {
        // dispatch(kernel, count) asks for `count` threads. The pass groups
        // them 64 at a time, so this launches ceil(count / 64) threadgroups.
        auto pass = commands.beginCompute();
        pass.dispatch(kernel, count);
    } // the pass ends here, closing its encoder
```

In a `GPUView`, the `Frame` gave you a command buffer each frame. Here
there's no frame, so we ask the device for one directly. `beginCompute()`
opens a **compute pass**, the compute equivalent of the render pass you
began with a clear colour, and `dispatch(kernel, count)` records "run this
kernel over `count` threads". Because the kernel used `threadId()`, it's a
1D kernel and takes one count. The pass is closed when it goes out of scope,
which is why it sits in its own braces.

```cpp
    // Hand the work to the GPU. Nothing has run until this point; recording a
    // dispatch only wrote a command into the buffer.
    commands.submit();

    // The GPU runs alongside the CPU, so the output is not ready yet. wait()
    // blocks this thread until the GPU has finished this command buffer.
    commands.wait();

    // Now the bytes are final: copy them back into CPU memory.
    commands.read(outputBuffer, result.data(), bytes);
    return true;
```

Here are the three steps from the diagram above, written out. `submit()`
hands the list to the GPU and returns at once. `wait()` blocks until it's
done. `commands.read(buffer, destination, bytes)` copies the output back into
our vector. (It would also wait by itself if we hadn't, but spelling `wait()`
out makes the cost visible.) EACP also has `commit()`, which is submit and
wait in one call, and `commitAsync()`, which returns a value you can attach a
continuation to instead of blocking.

### The CPU executor

```cpp
// The CPU path: the very same kernel object, interpreted on this thread over
// plain arrays. No device, no buffers, no waiting.
bool runOnCpu(SquareKernel& kernel, const Floats& input, Floats& result)
{
    auto executor = CpuCompute::Executor {kernel};

    if (!executor.isValid())
    {
        std::printf("CPU: cannot run this kernel: %s\n", executor.reason().c_str());
        return false;
    }

    // Bindings point the kernel's buffer members at our own memory.
    auto bindings = CpuCompute::Bindings {};
    bindings.set(kernel.input, std::span<const float> {input});
    bindings.set(kernel.output, std::span<float> {result});

    // The same dispatch shape as on the GPU: count threads in groups of 64.
    return executor.dispatch(bindings, count);
}
```

`CpuCompute::Executor {kernel}` reads the kernel's recorded graph and builds
a plan for running it, allocating all the scratch memory it'll ever need. A
few features have no CPU form (sampling textures, for instance), and for
those `isValid()` is false and `reason()` says why. Ours is plain arithmetic,
so it's fine.

Instead of GPU buffers, `Bindings` points each buffer member at memory we
already own: a `std::span<const float>` for an input, a `std::span<float>`
for an output. `set` stores only a pointer and a length, so it's free to
call again with new arrays. `dispatch(bindings, count)` then runs the same
grid of 1563 groups of 64 threads, right here on this thread, and returns
when it's finished. Nothing to submit and nothing to wait for. The uniform
needs no special handling: the executor reads `kernel.offset` from the
kernel when it dispatches.

The CPU path also needs one extra library, so the chapter's
`CMakeLists.txt` links `eacp-cpu-compute` next to `eacp-gpu`. Since the
program opens no window, it's also not marked as a GUI app, which on Windows
keeps its console output visible.

### Running both and comparing

```cpp
    auto onGpu = Floats((std::size_t) count);
    auto gpuRan = runOnGpu(kernel, input, onGpu);

    if (gpuRan)
        printResult("GPU", onGpu);

    auto onCpu = Floats((std::size_t) count);

    if (!runOnCpu(kernel, input, onCpu))
        return 1;

    printResult("CPU", onCpu);

    if (!gpuRan)
        return 0;

    // Floating point on two different processors can differ in the last bit
    // (the GPU may fuse x * x + offset into one instruction), so compare with
    // a small tolerance rather than ==.
    auto difference = maxDifference(onGpu, onCpu);
    auto match = difference <= tolerance;
```

One kernel object, two runs. Then the comparison, and note that it isn't
`==`. The GPU is allowed to compute `x * x + offset` as a single **fused
multiply-add**, which rounds once instead of twice, while the CPU executor
deliberately never fuses. The answers can therefore differ in the last bit
of the float. This is normal floating point behaviour, not a bug, and any
GPU-versus-CPU test has to allow for it.

```cpp
// The console counterpart of LEARNEACP_CHAPTER. As main(), it calls
// Apps::runConsole, which runs runCompute once inside EACP's event loop, then
// exits with its return value. No window is ever opened.
LEARNEACP_CONSOLE_CHAPTER(Compute::runCompute)
```

Where every windowed chapter ends with `LEARNEACP_CHAPTER`, this one ends with
its console counterpart. Written by hand it is this `main`:

<!-- snippet: illustrative -->
```cpp
int main()
{
    return Apps::runConsole(Compute::runCompute);
}
```

Instead of `runWindowedApp`, `Apps::runConsole` starts EACP's event loop,
runs our function once, and exits with whatever it returned.

### What you should see

```
Kernel: out[i] = in[i] * in[i] + 0.5
Grid: 100000 threads in groups of 64 = 1563 threadgroups

GPU first values: 25.5000 25.4001 25.3004 25.2009 25.1016
GPU checksum: 883334.9606

CPU first values: 25.5000 25.4001 25.3004 25.2009 25.1016
CPU checksum: 883334.9607

GPU vs CPU: max difference 1.90735e-06, results match
```

The input starts at -5.0 and climbs by 0.01, so the first output is
(-5)^2 + 0.5 = 25.5. The largest difference, 1.9e-6, is exactly one step of
float precision for numbers between 16 and 32: one rounding, one last bit.

## When is compute worth it?

Be honest with yourself about this one. For our program the answer is "not
really". Squaring 100,000 floats takes the CPU well under a millisecond,
which is about what the submit, wait and read round trip costs on its own.
The GPU wins when:

- **N is large.** Millions of elements, not thousands, so the parallel work
  dwarfs the fixed cost of launching it.
- **Each element does real work.** A few hundred instructions per thread
  (physics, image filters, matrix products) instead of one multiply.
- **The data stays on the GPU.** The biggest win is never reading back at
  all: a kernel writes particle positions or a texture, and the very next
  draw consumes it in the same frame. No wait, no copy.

And it loses when N is small, the work is serial (each step needs the
previous one), or you need the answer on the CPU right away, every time, for
tiny jobs. Then the round trip is the whole cost. Measure before you move
something to the GPU.

## Try it yourself

1. **Change the function.** Make the kernel compute `sin(x) * offset`, or
   clamp negative inputs to zero. *Hint: `sin`, `max` and `abs` work on
   handles just as they did in the fragment chapters; only `define()`
   changes, and the CPU still agrees.*
2. **Use the group ids.** Write `toFloat(groupId())` into the output instead
   and print elements 60 to 70. *Hint: you should see the value jump from 0
   to 1 at element 64; try `localId()` too.*
3. **Two inputs.** Add a second `Uniform<InputBuffer>` and compute
   `a[i] * b[i]`. *Hint: add it to `EACP_SHADER`, make a second buffer on the
   GPU side and a second `bindings.set` on the CPU side.*
4. **Time it.** Wrap both paths with `std::chrono::steady_clock` and try
   `count` at 1,000, 100,000 and 10,000,000. *Hint: time the GPU's
   submit-to-read separately from its buffer creation; see where the curves
   cross. EACP's `Apps/GPU/CpuCompute` example does exactly this.*
5. **Feed a draw.** Put the kernel inside a `GPUView`: each frame, dispatch
   it to write positions into a buffer, then draw from that same buffer
   without ever reading it back. *Hint: `frame.beginCompute()` gives you a
   compute pass inside the frame; a `ShaderProgram` can declare a
   `Uniform<InputBuffer>` and index it, or use the buffer as per-instance
   data. `Apps/GPU/ComputeParticles` is a complete version of this.*

## Further reading

- EACP GPU README, [Compute](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#compute):
  2D and 3D grids, `threadPosition()`, `commit` versus `commitAsync`.
- [The group a kernel is dispatched in](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#the-group-a-kernel-is-dispatched-in):
  choosing your own threadgroup shape, and why wider isn't always faster.
- [Waiting for one command buffer](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#waiting-for-one-command-buffer):
  `submit`, `wait` and keeping several command buffers in flight.
- [Buffers of integers](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#buffers-of-integers)
  and [Threadgroup memory](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#threadgroup-memory).
- [Running a kernel on the CPU](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md#running-a-kernel-on-the-cpu):
  the executor in depth, splitting a dispatch over threads, and the realtime
  contract.
- EACP examples [`Apps/GPU/Compute`](https://github.com/eyalamirmusic/eacp/tree/main/Apps/GPU/Compute)
  (chained kernels generating audio) and
  [`Apps/GPU/CpuCompute`](https://github.com/eyalamirmusic/eacp/tree/main/Apps/GPU/CpuCompute)
  (GPU and CPU compared and timed).
- [LearnOpenGL: Compute Shaders](https://learnopengl.com/Guest-Articles/2022/Compute-Shaders/Introduction)
  covers the same ideas from the OpenGL side, including work groups.

## Where to go from here

This chapter closes the first part of the series, and you now have the whole
basic toolkit: the render pipeline, uniforms, textures, transformations,
lighting and compute. The chapters that follow build on it with cameras,
materials, offscreen passes and more advanced lighting. Alongside them, it
pays to read real programs. EACP's
[`Apps/GPU`](https://github.com/eyalamirmusic/eacp/tree/main/Apps/GPU)
directory is full of them, each short and focused:

- **ComputeParticles**: tens of thousands of particles moved by a kernel and
  drawn by the very next pass, with no readback at all.
- **ComputeImage**: a kernel that writes a texture, which a draw then samples.
- **Instancing**: one mesh drawn many times from a per-instance buffer, the
  natural partner of a kernel that writes that buffer.

And when you want to know what else the EDSL can do, or why it refuses
something, the [GPU README](https://github.com/eyalamirmusic/eacp/blob/main/Lib/eacp/GPU/README.md)
is the reference everything in this series was built from.

Previous: [09. Lighting](../09-Lighting/README.md) | Next: [11. Camera](../11-Camera/README.md)
