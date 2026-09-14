#include "PyinPitchDetector.hpp"
#include "CepstrumPitchDetector.hpp"
#include "DetectorFusion.hpp"
#include "Pipeline.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static constexpr float BENCH_SR = 48000.0f;
static constexpr int   WARMUP   = 10;
static constexpr int   ITERS    = 200;
static constexpr float BENCH_PI = 3.14159265358979f;

static std::vector<float> makeSine(float freq, int frameSize) {
    std::vector<float> buf(frameSize);
    for (int i = 0; i < frameSize; ++i)
        buf[i] = 0.8f * std::sin(2.0f * BENCH_PI * freq * static_cast<float>(i) / BENCH_SR);
    return buf;
}

static double nsPerFrame(std::chrono::high_resolution_clock::time_point t0,
                         std::chrono::high_resolution_clock::time_point t1) {
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(ITERS);
}

template <typename ProcessFn>
static double benchmark(ProcessFn&& process) {
    for (int i = 0; i < WARMUP; ++i) process();
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) process();
    auto t1 = std::chrono::high_resolution_clock::now();
    return nsPerFrame(t0, t1);
}

static std::unique_ptr<DetectorFusion> makeFusion(int frameSize) {
    return std::make_unique<DetectorFusion>(
        std::make_unique<PyinPitchDetector>(BENCH_SR, frameSize),
        std::make_unique<CepstrumPitchDetector>(BENCH_SR, frameSize));
}

static void benchFrameSize(int frameSize, float freq, const char* label) {
    const auto buf = makeSine(freq, frameSize);

    double ns_pyin = 0.0;
    {
        PyinPitchDetector pyin(BENCH_SR, frameSize);
        ns_pyin = benchmark([&] { pyin.detect(buf.data(), frameSize, BENCH_SR); });
    }

    double ns_cep = 0.0;
    {
        CepstrumPitchDetector cep(BENCH_SR, frameSize);
        ns_cep = benchmark([&] { cep.detect(buf.data(), frameSize, BENCH_SR); });
    }

    double ns_pipeline = 0.0;
    {
        Pipeline pipeline(frameSize, BENCH_SR, makeFusion(frameSize));
        ns_pipeline = benchmark([&] { pipeline.process(buf.data(), frameSize); });
    }

    std::printf("\nBenchmark  (frame=%d @ %.0f Hz, %d iterations)%s\n",
                frameSize, BENCH_SR, ITERS, label);
    std::printf("  pYIN         : %8.1f ns/frame  (%5.2f ms)\n", ns_pyin,     ns_pyin     * 1e-6);
    std::printf("  Cepstrum     : %8.1f ns/frame  (%5.2f ms)\n", ns_cep,      ns_cep      * 1e-6);
    std::printf("  Full pipeline: %8.1f ns/frame  (%5.2f ms)\n", ns_pipeline, ns_pipeline * 1e-6);

    if (frameSize == 4096) {
        std::printf("  Budget (75%% overlap, hop=1024): %.2f ms/hop → %.1f%% CPU\n",
                    ns_pipeline * 1e-6,
                    (ns_pipeline * 1e-6) / (1024.0 / BENCH_SR * 1000.0) * 100.0);
    }
}

int main() {
    benchFrameSize(2048, 440.0f, "");
    benchFrameSize(4096, 82.41f, " [bass/cello mode]");
    return 0;
}
