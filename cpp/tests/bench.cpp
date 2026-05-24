#include "YinPitchDetector.hpp"
#include "PyinPitchDetector.hpp"
#include "CepstrumPitchDetector.hpp"
#include "EnsembleSelector.hpp"
#include "Pipeline.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static constexpr float BENCH_SR    = 48000.0f;
static constexpr int   BENCH_FRAME = 2048;
static constexpr int   WARMUP      = 10;
static constexpr int   ITERS       = 200;
static constexpr float BENCH_PI    = 3.14159265358979f;

static std::vector<float> makeSine(float freq) {
    std::vector<float> buf(BENCH_FRAME);
    for (int i = 0; i < BENCH_FRAME; ++i)
        buf[i] = 0.8f * std::sin(2.0f * BENCH_PI * freq * static_cast<float>(i) / BENCH_SR);
    return buf;
}

static double nsPerFrame(std::chrono::high_resolution_clock::time_point t0,
                         std::chrono::high_resolution_clock::time_point t1) {
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(ITERS);
}

int main() {
    const auto buf = makeSine(440.0f);

    // --- YIN ---
    double ns_yin = 0.0;
    {
        YinPitchDetector yin(BENCH_SR, BENCH_FRAME);
        for (int i = 0; i < WARMUP; ++i) yin.detect(buf.data(), BENCH_FRAME, BENCH_SR);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) yin.detect(buf.data(), BENCH_FRAME, BENCH_SR);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_yin = nsPerFrame(t0, t1);
    }

    // --- PYIN ---
    double ns_pyin = 0.0;
    {
        PyinPitchDetector pyin(BENCH_SR, BENCH_FRAME);
        for (int i = 0; i < WARMUP; ++i) pyin.detect(buf.data(), BENCH_FRAME, BENCH_SR);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) pyin.detect(buf.data(), BENCH_FRAME, BENCH_SR);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_pyin = nsPerFrame(t0, t1);
    }

    // --- Cepstrum ---
    double ns_cep = 0.0;
    {
        CepstrumPitchDetector cep(BENCH_SR, BENCH_FRAME);
        for (int i = 0; i < WARMUP; ++i) cep.detect(buf.data(), BENCH_FRAME, BENCH_SR);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) cep.detect(buf.data(), BENCH_FRAME, BENCH_SR);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_cep = nsPerFrame(t0, t1);
    }

    // --- Full pipeline (HPF + Window + Ensemble + SNR + PostProcessor) ---
    double ns_pipeline = 0.0;
    {
        std::vector<std::unique_ptr<IPitchDetector>> dets;
        dets.push_back(std::make_unique<YinPitchDetector>(BENCH_SR, BENCH_FRAME));
        dets.push_back(std::make_unique<PyinPitchDetector>(BENCH_SR, BENCH_FRAME));
        dets.push_back(std::make_unique<CepstrumPitchDetector>(BENCH_SR, BENCH_FRAME));
        auto ensemble = std::make_unique<EnsembleSelector>(std::move(dets));
        Pipeline pipeline(BENCH_FRAME, BENCH_SR, std::move(ensemble));

        for (int i = 0; i < WARMUP; ++i) pipeline.process(buf.data(), BENCH_FRAME);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) pipeline.process(buf.data(), BENCH_FRAME);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_pipeline = nsPerFrame(t0, t1);
    }

    std::printf("Benchmark  (frame=%d @ %.0f Hz, %d iterations)\n",
                BENCH_FRAME, BENCH_SR, ITERS);
    std::printf("  YIN          : %8.1f ns/frame  (%5.2f ms)\n", ns_yin,      ns_yin      * 1e-6);
    std::printf("  PYIN         : %8.1f ns/frame  (%5.2f ms)\n", ns_pyin,     ns_pyin     * 1e-6);
    std::printf("  Cepstrum     : %8.1f ns/frame  (%5.2f ms)\n", ns_cep,      ns_cep      * 1e-6);
    std::printf("  Full pipeline: %8.1f ns/frame  (%5.2f ms)\n", ns_pipeline, ns_pipeline * 1e-6);

    // --- 4096 frame benchmarks (bass/cello mode) ---
    static constexpr int BENCH_FRAME_4096 = 4096;

    auto makeSine4096 = [](float freq) {
        std::vector<float> b(BENCH_FRAME_4096);
        for (int i = 0; i < BENCH_FRAME_4096; ++i)
            b[i] = 0.8f * std::sin(2.0f * BENCH_PI * freq * static_cast<float>(i) / BENCH_SR);
        return b;
    };

    const auto buf4096 = makeSine4096(82.41f); // E2 for bass benchmark

    double ns_yin4096 = 0.0;
    {
        YinPitchDetector yin(BENCH_SR, BENCH_FRAME_4096);
        for (int i = 0; i < WARMUP; ++i) yin.detect(buf4096.data(), BENCH_FRAME_4096, BENCH_SR);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) yin.detect(buf4096.data(), BENCH_FRAME_4096, BENCH_SR);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_yin4096 = nsPerFrame(t0, t1);
    }

    double ns_pyin4096 = 0.0;
    {
        PyinPitchDetector pyin(BENCH_SR, BENCH_FRAME_4096);
        for (int i = 0; i < WARMUP; ++i) pyin.detect(buf4096.data(), BENCH_FRAME_4096, BENCH_SR);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) pyin.detect(buf4096.data(), BENCH_FRAME_4096, BENCH_SR);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_pyin4096 = nsPerFrame(t0, t1);
    }

    double ns_cep4096 = 0.0;
    {
        CepstrumPitchDetector cep(BENCH_SR, BENCH_FRAME_4096);
        for (int i = 0; i < WARMUP; ++i) cep.detect(buf4096.data(), BENCH_FRAME_4096, BENCH_SR);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) cep.detect(buf4096.data(), BENCH_FRAME_4096, BENCH_SR);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_cep4096 = nsPerFrame(t0, t1);
    }

    double ns_pipeline4096 = 0.0;
    {
        std::vector<std::unique_ptr<IPitchDetector>> dets;
        dets.push_back(std::make_unique<YinPitchDetector>(BENCH_SR, BENCH_FRAME_4096));
        dets.push_back(std::make_unique<PyinPitchDetector>(BENCH_SR, BENCH_FRAME_4096));
        dets.push_back(std::make_unique<CepstrumPitchDetector>(BENCH_SR, BENCH_FRAME_4096));
        auto ensemble = std::make_unique<EnsembleSelector>(std::move(dets));
        Pipeline pipeline(BENCH_FRAME_4096, BENCH_SR, std::move(ensemble));

        for (int i = 0; i < WARMUP; ++i) pipeline.process(buf4096.data(), BENCH_FRAME_4096);
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < ITERS; ++i) pipeline.process(buf4096.data(), BENCH_FRAME_4096);
        auto t1 = std::chrono::high_resolution_clock::now();
        ns_pipeline4096 = nsPerFrame(t0, t1);
    }

    std::printf("\nBenchmark  (frame=%d @ %.0f Hz, %d iterations) [bass/cello mode]\n",
                BENCH_FRAME_4096, BENCH_SR, ITERS);
    std::printf("  YIN          : %8.1f ns/frame  (%5.2f ms)\n", ns_yin4096,      ns_yin4096      * 1e-6);
    std::printf("  PYIN         : %8.1f ns/frame  (%5.2f ms)\n", ns_pyin4096,     ns_pyin4096     * 1e-6);
    std::printf("  Cepstrum     : %8.1f ns/frame  (%5.2f ms)\n", ns_cep4096,      ns_cep4096      * 1e-6);
    std::printf("  Full pipeline: %8.1f ns/frame  (%5.2f ms)\n", ns_pipeline4096, ns_pipeline4096 * 1e-6);
    std::printf("  Budget (75%% overlap, hop=1024): %.2f ms/hop → %.1f%% CPU\n",
                ns_pipeline4096 * 1e-6,
                (ns_pipeline4096 * 1e-6) / (1024.0 / BENCH_SR * 1000.0) * 100.0);

    return 0;
}
