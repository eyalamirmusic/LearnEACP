// Chapter 10: Compute. One kernel, run on the GPU and then on the CPU, with
// the two results compared. No window: a compute job has nothing to draw, so
// this is a console program that prints what it computed and exits.
#include <eacp/GPU/CpuCompute/CpuCompute.h>
#include <eacp/GPU/GPU.h>
#include <LearnEACP/Chapter.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

using namespace eacp;
using namespace GPU;

namespace Compute
{
namespace
{
// Deliberately not a multiple of the group width (64), to show that the last,
// partly filled group is safe: threads past the end simply do nothing.
constexpr auto count = 100000;
constexpr auto bytes = (std::int64_t) sizeof(float) * count;
constexpr auto tolerance = 1e-5f;

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

using Floats = std::vector<float>;

Floats makeInput()
{
    auto values = Floats((std::size_t) count);

    for (auto i = 0; i < count; ++i)
        values[(std::size_t) i] = (float) (i % 1000) * 0.01f - 5.0f;

    return values;
}

double checksum(const Floats& values)
{
    auto sum = 0.0;

    for (auto value: values)
        sum += (double) value;

    return sum;
}

void printResult(const char* label, const Floats& values)
{
    std::printf("%s first values:", label);

    for (auto i = 0; i < 5; ++i)
        std::printf(" %.4f", (double) values[(std::size_t) i]);

    std::printf("\n%s checksum: %.4f\n\n", label, checksum(values));
}

// The GPU path: upload, record, submit, wait, read back.
bool runOnGpu(SquareKernel& kernel, const Floats& input, Floats& result)
{
    auto& device = Device::shared();

    if (!device.isValid())
    {
        std::printf("GPU: no device available, skipping.\n\n");
        return false;
    }

    // Two buffers in GPU memory: one filled from our array, one left empty
    // for the kernel to write into.
    auto inputBuffer = device.makeBuffer(input.data(), bytes, BufferUsage::Storage);
    auto outputBuffer = device.makeBuffer(bytes);

    kernel.input = inputBuffer;
    kernel.output = outputBuffer;

    // Compiles the emitted source into a compute pipeline on this device.
    kernel.prepare(device);

    // A command buffer outside any frame: we are not drawing, just computing.
    auto commands = device.makeCommandBuffer();

    {
        // dispatch(kernel, count) asks for `count` threads. The pass groups
        // them 64 at a time, so this launches ceil(count / 64) threadgroups.
        auto pass = commands.beginCompute();
        pass.dispatch(kernel, count);
    } // the pass ends here, closing its encoder

    // Hand the work to the GPU. Nothing has run until this point; recording a
    // dispatch only wrote a command into the buffer.
    commands.submit();

    // The GPU runs alongside the CPU, so the output is not ready yet. wait()
    // blocks this thread until the GPU has finished this command buffer.
    commands.wait();

    // Now the bytes are final: copy them back into CPU memory.
    commands.read(outputBuffer, result.data(), bytes);
    return true;
}

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

float maxDifference(const Floats& a, const Floats& b)
{
    auto largest = 0.0f;

    for (auto i = 0; i < count; ++i)
        largest =
            std::max(largest, std::abs(a[(std::size_t) i] - b[(std::size_t) i]));

    return largest;
}

int runCompute()
{
    auto input = makeInput();

    auto kernel = SquareKernel {};
    kernel.offset = 0.5f;

    auto group = kernel.groupShape().x;
    std::printf("Kernel: out[i] = in[i] * in[i] + 0.5\n");
    std::printf("Grid: %d threads in groups of %d = %d threadgroups\n\n",
                count,
                group,
                (count + group - 1) / group);

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

    std::printf("GPU vs CPU: max difference %g, %s\n",
                (double) difference,
                match ? "results match" : "results DIFFER");

    return match ? 0 : 1;
}
} // namespace
} // namespace Compute

// The console counterpart of LEARNEACP_CHAPTER. As main(), it calls
// Apps::runConsole, which runs runCompute once inside EACP's event loop, then
// exits with its return value. No window is ever opened.
LEARNEACP_CONSOLE_CHAPTER(Compute::runCompute)
