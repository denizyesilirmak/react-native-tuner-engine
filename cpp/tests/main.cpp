#include "NoteMapper.hpp"
#include "OnsetDetector.hpp"
#include "TunerEngine.hpp"
#include "PyinPitchDetector.hpp"
#include "CepstrumPitchDetector.hpp"
#include "DetectorFusion.hpp"
#include "BiquadHpf.hpp"
#include "AudioFrameDispatcher.hpp"
#include "InstrumentPresets.hpp"
#include "SnrEstimator.hpp"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

static constexpr float PI = 3.14159265358979323846f;

static void assertNear(float actual, float expected, float tolerance) {
    assert(std::fabs(actual - expected) <= tolerance);
}

static std::vector<float> generateSine(
    float frequency,
    float sampleRate,
    int frameSize,
    float amplitude = 0.8f
) {
    std::vector<float> buffer(frameSize);

    for (int i = 0; i < frameSize; ++i) {
        buffer[i] = amplitude * std::sin(2.0f * PI * frequency * i / sampleRate);
    }

    return buffer;
}

static std::vector<float> generateSilence(int frameSize) {
    return std::vector<float>(frameSize, 0.0f);
}

// Fundamental plus decaying harmonics — closer to a plucked string than a pure
// sine, and rich enough for the cepstrum detector to find a quefrency peak.
static std::vector<float> generateHarmonicTone(
    float fundamental,
    float sampleRate,
    int sampleCount
) {
    std::vector<float> buffer(sampleCount, 0.0f);

    for (int harmonic = 1; harmonic <= 5; ++harmonic) {
        const float amplitude = 0.5f / static_cast<float>(harmonic);
        for (int i = 0; i < sampleCount; ++i) {
            buffer[i] += amplitude
                       * std::sin(2.0f * PI * fundamental * harmonic * i / sampleRate);
        }
    }

    return buffer;
}

static void testNoteMapper() {
    NoteMapper mapper(440.0f);

    {
        auto result = mapper.map(82.41f, 1.0f, -12.0f);
        assert(result.hasPitch);
        assert(result.noteName == "E");
        assert(result.octave == 2);
        assertNear(result.cents, 0.0f, 1.0f);
    }

    {
        auto result = mapper.map(110.0f, 1.0f, -12.0f);
        assert(result.hasPitch);
        assert(result.noteName == "A");
        assert(result.octave == 2);
        assertNear(result.cents, 0.0f, 1.0f);
    }

    {
        auto result = mapper.map(329.63f, 1.0f, -12.0f);
        assert(result.hasPitch);
        assert(result.noteName == "E");
        assert(result.octave == 4);
        assertNear(result.cents, 0.0f, 1.0f);
    }

    {
        auto result = mapper.map(440.0f, 1.0f, -12.0f);
        assert(result.hasPitch);
        assert(result.noteName == "A");
        assert(result.octave == 4);
        assertNear(result.cents, 0.0f, 1.0f);
    }
}

static void testPyin(float inputFrequency) {
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize = 4096;

    auto buffer = generateSine(inputFrequency, sampleRate, frameSize);

    PyinPitchDetector detector(sampleRate, frameSize);
    auto result = detector.detect(buffer.data(), static_cast<int>(buffer.size()), sampleRate);

    std::cout
        << "pyin input: " << inputFrequency
        << " detected: " << result.frequency
        << " confidence: " << result.confidence
        << std::endl;

    assert(result.voiced);
    assertNear(result.frequency, inputFrequency, 0.5f);
    assert(result.confidence > 0.80f);
}

static void testTunerEngine(
    float inputFrequency,
    const std::string& expectedNote,
    int expectedOctave
) {
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize = 4096;

    auto buffer = generateSine(inputFrequency, sampleRate, frameSize);

    TunerEngine engine(sampleRate, frameSize);
    auto result = engine.process(buffer.data(), static_cast<int>(buffer.size()));

    std::cout
        << "engine input: " << inputFrequency
        << " detected: " << result.frequency
        << " note: " << result.noteName << result.octave
        << " cents: " << result.cents
        << " confidence: " << result.confidence
        << " rms: " << result.rmsDb
        << std::endl;

    assert(result.hasPitch);
    assertNear(result.frequency, inputFrequency, 0.5f);
    assert(result.noteName == expectedNote);
    assert(result.octave == expectedOctave);
    assert(std::fabs(result.cents) < 5.0f);
    assert(result.confidence > 0.75f);
}

static void testTunerEngineSilence() {
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize = 4096;

    auto buffer = generateSilence(frameSize);

    TunerEngine engine(sampleRate, frameSize);
    auto result = engine.process(buffer.data(), static_cast<int>(buffer.size()));

    std::cout
        << "silence hasPitch: " << result.hasPitch
        << " rms: " << result.rmsDb
        << std::endl;

    assert(!result.hasPitch);
    assert(result.rmsDb <= -100.0f);
}

static void testTunerEngineQuietSignal() {
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize = 4096;

    auto buffer = generateSine(110.0f, sampleRate, frameSize, 0.0001f);

    TunerEngine engine(sampleRate, frameSize);
    engine.setNoiseGateDb(-55.0f);

    auto result = engine.process(buffer.data(), static_cast<int>(buffer.size()));

    std::cout
        << "quiet signal hasPitch: " << result.hasPitch
        << " rms: " << result.rmsDb
        << std::endl;

    assert(!result.hasPitch);
}

// --- M2: Pipeline / DSP hardening tests ---

static void testPipelineCleanSine(float inputFrequency, const std::string& expectedNote, int expectedOctave) {
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize    = 4096;
    constexpr int numFrames    = 6;

    // Generate a single continuous sine (no phase discontinuity between frames)
    // so the HPF state transitions correctly across frame boundaries.
    auto continuous = generateSine(inputFrequency, sampleRate, frameSize * numFrames);

    TunerEngine engine(sampleRate, frameSize);
    PitchResult result;
    for (int f = 0; f < numFrames; ++f) {
        result = engine.process(continuous.data() + f * frameSize, frameSize);
    }

    std::cout
        << "pipeline sine " << inputFrequency << " Hz"
        << " → " << result.noteName << result.octave
        << " cents=" << result.cents
        << " conf=" << result.confidence << "\n";

    assert(result.hasPitch);
    assert(result.noteName == expectedNote);
    assert(result.octave == expectedOctave);
    assertNear(result.cents, 0.0f, 5.0f);
}

static void testPipelineNoisySignal() {
    // Pure noise should NOT produce a stable pitch result
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize    = 4096;

    TunerEngine engine(sampleRate, frameSize);
    std::vector<float> buffer(frameSize);

    // Pink-ish noise via simple LFSR-style scramble
    uint32_t seed = 0xDEADBEEF;
    for (int i = 0; i < frameSize; ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        buffer[i] = static_cast<float>(static_cast<int32_t>(seed)) / 2147483648.0f * 0.3f;
    }

    PitchResult result;
    for (int f = 0; f < 6; ++f) {
        result = engine.process(buffer.data(), static_cast<int>(buffer.size()));
    }

    std::cout
        << "noisy signal hasPitch=" << result.hasPitch
        << " conf=" << result.confidence << "\n";

    // Noisy signal should have low confidence / no stable pitch
    assert(!result.hasPitch || result.confidence < 0.50f);
}

static void testPipelineHysteresis() {
    // Play A4 for several frames, then switch to B4 — verify debounce
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize    = 4096;
    constexpr float freqA4     = 440.0f;
    constexpr float freqB4     = 493.88f;

    TunerEngine engine(sampleRate, frameSize);
    PitchResult result;

    // Stabilise on A4 — continuous sine
    auto contA4 = generateSine(freqA4, sampleRate, frameSize * 6);
    for (int f = 0; f < 6; ++f) {
        result = engine.process(contA4.data() + f * frameSize, frameSize);
    }
    assert(result.hasPitch);
    assert(result.noteName == "A");

    // Switch to B4 — continuous sine.
    // Median buffer (5) fills with B4 at frame 3 of the new note.
    // Then hysteresis counter (3) confirms → lock at frame 5 (3 frames where median == B4).
    // Feed 8 frames total to be comfortably past the switch.
    auto contB4 = generateSine(freqB4, sampleRate, frameSize * 8);

    result = engine.process(contB4.data(), frameSize);
    std::cout
        << "hysteresis after 1 B4 frame: note=" << result.noteName << "\n";

    for (int f = 1; f < 8; ++f) {
        result = engine.process(contB4.data() + f * frameSize, frameSize);
    }
    std::cout
        << "hysteresis after 8 B4 frames: note=" << result.noteName << "\n";
    assert(result.hasPitch);
    assert(result.noteName == "B");
}

static void testInstrumentPreset() {
    TunerEngine engine(48000.0f, 4096);
    engine.setInstrument("guitar");

    // E2 (82 Hz) — within guitar range, continuous sine
    PitchResult result;
    auto contE2 = generateSine(82.41f, 48000.0f, 4096 * 6);
    for (int f = 0; f < 6; ++f) {
        result = engine.process(contE2.data() + f * 4096, 4096);
    }
    std::cout << "guitar preset E2: hasPitch=" << result.hasPitch
              << " note=" << result.noteName << result.octave << "\n";
    assert(result.hasPitch);

    // Switch to ukulele — G4 should work, E2 might not (too low)
    engine.setInstrument("ukulele");
    auto contG4 = generateSine(392.0f, 48000.0f, 4096 * 6);
    for (int f = 0; f < 6; ++f) {
        result = engine.process(contG4.data() + f * 4096, 4096);
    }
    std::cout << "ukulele preset G4: hasPitch=" << result.hasPitch
              << " note=" << result.noteName << result.octave << "\n";
    assert(result.hasPitch);
    assert(result.noteName == "G");
}

// --- M3: per-detector and fusion tests ---

static void testDetectorOnSine(float freq, const std::string& label) {
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;
    auto buf = generateSine(freq, sr, n);

    BiquadHpf hpf(sr, 70.0f);
    hpf.process(buf.data(), n);

    PyinPitchDetector pyin(sr, n);
    auto result = pyin.detect(buf.data(), n, sr);

    std::cout << "detector " << label << " @ " << freq << " Hz:"
              << " pyin freq=" << result.frequency
              << " conf=" << result.confidence << "\n";

    assert(result.voiced);
    assertNear(result.frequency, freq, freq * 0.01f); // within 1%
}

static void testCepstrumOnHarmonicTone() {
    // The cepstrum detector needs harmonics to find a quefrency peak;
    // a plucked-string-like tone is its home turf.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;
    constexpr float f0 = 196.0f; // G3

    auto buf = generateHarmonicTone(f0, sr, n);

    CepstrumPitchDetector cepstrum(sr, n);
    auto result = cepstrum.detect(buf.data(), n, sr);

    std::cout << "cepstrum harmonic G3: freq=" << result.frequency
              << " conf=" << result.confidence << "\n";

    assert(result.voiced);
    // The cepstrum is the coarse corroborator — 3% is enough to land well
    // inside the fusion's one-semitone (~6%) agreement window.
    assertNear(result.frequency, f0, f0 * 0.03f);
}

// Fixed-output detector for exercising DetectorFusion's decision logic in isolation.
struct StubDetector : IPitchDetector {
    DetectorResult fixedResult;
    explicit StubDetector(DetectorResult result) : fixedResult(result) {}
    DetectorResult detect(const float*, int, float) override { return fixedResult; }
    void setFrequencyRange(float, float) override {}
};

static void testFusionDecisions() {
    const float dummyFrame[8] = {};

    // Agreement: report the primary's precise frequency, combine confidences
    // as independent evidence: 1 - (1-0.8)(1-0.5) = 0.9.
    {
        DetectorFusion fusion(
            std::make_unique<StubDetector>(DetectorResult{true, 440.0f, 0.8f}),
            std::make_unique<StubDetector>(DetectorResult{true, 442.0f, 0.5f}));
        auto r = fusion.detect(dummyFrame, 8, 48000.0f);
        assert(r.voiced);
        assert(r.frequency == 440.0f); // exactly the primary's, never averaged
        assertNear(r.confidence, 0.9f, 0.001f);
    }

    // Clash (an octave apart): the primary's frequency survives — the coarse
    // corroborator never overrides it — but its conviction dampens the
    // confidence: 0.9 * (1 - 0.4 * 0.3) = 0.792.
    {
        DetectorFusion fusion(
            std::make_unique<StubDetector>(DetectorResult{true, 220.0f, 0.9f}),
            std::make_unique<StubDetector>(DetectorResult{true, 440.0f, 0.3f}));
        auto r = fusion.detect(dummyFrame, 8, 48000.0f);
        assert(r.voiced);
        assert(r.frequency == 220.0f);
        assertNear(r.confidence, 0.792f, 0.001f);
    }

    // Solo primary: mild dampening (0.8 * 0.9 = 0.72).
    {
        DetectorFusion fusion(
            std::make_unique<StubDetector>(DetectorResult{true, 330.0f, 0.8f}),
            std::make_unique<StubDetector>(DetectorResult{}));
        auto r = fusion.detect(dummyFrame, 8, 48000.0f);
        assert(r.voiced);
        assert(r.frequency == 330.0f);
        assertNear(r.confidence, 0.72f, 0.001f);
    }

    // Solo corroborator: stronger dampening (0.8 * 0.7 = 0.56).
    {
        DetectorFusion fusion(
            std::make_unique<StubDetector>(DetectorResult{}),
            std::make_unique<StubDetector>(DetectorResult{true, 330.0f, 0.8f}));
        auto r = fusion.detect(dummyFrame, 8, 48000.0f);
        assert(r.voiced);
        assert(r.frequency == 330.0f);
        assertNear(r.confidence, 0.56f, 0.001f);
    }

    // Neither voiced → unvoiced.
    {
        DetectorFusion fusion(
            std::make_unique<StubDetector>(DetectorResult{}),
            std::make_unique<StubDetector>(DetectorResult{}));
        auto r = fusion.detect(dummyFrame, 8, 48000.0f);
        assert(!r.voiced);
    }

    std::cout << "fusion decisions: all correct\n";
}

static void testFusionAgreementOnHarmonicTone() {
    // On a harmonic-rich tone both detectors fire and agree, so the fused
    // confidence must exceed what either detector reports alone.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;
    constexpr float f0 = 196.0f; // G3

    auto buf = generateHarmonicTone(f0, sr, n * 6); // continuous for HPF warmup

    DetectorFusion fusion(
        std::make_unique<PyinPitchDetector>(sr, n),
        std::make_unique<CepstrumPitchDetector>(sr, n));

    BiquadHpf hpf(sr, 70.0f);

    DetectorResult result;
    for (int f = 0; f < 6; ++f) {
        std::vector<float> frame(buf.begin() + f * n, buf.begin() + (f + 1) * n);
        hpf.process(frame.data(), n);
        result = fusion.detect(frame.data(), n, sr);
    }

    std::cout << "fusion harmonic G3: voiced=" << result.voiced
              << " freq=" << result.frequency
              << " conf=" << result.confidence << "\n";

    assert(result.voiced);
    assertNear(result.frequency, f0, f0 * 0.01f);
    assert(result.confidence > 0.85f); // corroboration raised the confidence
}

static void testEngineOctaveSafety() {
    // D3 (146.83 Hz): tau0 and 2*tau0 both fit in the search range.
    // The engine must return the fundamental, not the sub-octave.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;

    TunerEngine engine(sr, n);
    auto buf = generateSine(146.83f, sr, n * 6);

    PitchResult result;
    for (int f = 0; f < 6; ++f) {
        result = engine.process(buf.data() + f * n, n);
    }

    std::cout << "engine octave safety D3: "
              << result.noteName << result.octave
              << " freq=" << result.frequency << "\n";

    assert(result.hasPitch);
    assert(result.noteName == "D");
    assert(result.octave == 3);
}

static void testOnsetDetector() {
    OnsetDetector::Config cfg;
    cfg.thresholdDb = 6.0f;
    cfg.envelopeAlpha = 0.15f;
    cfg.cooldownFrames = 4;

    OnsetDetector det(cfg);

    // When disabled, never fires
    assert(!det.detect(-30.0f));
    assert(!det.detect(0.0f)); // 30dB jump — still no onset because disabled

    // Enable
    det.setEnabled(true);
    det.reset();

    // Feed quiet frames to establish envelope
    for (int i = 0; i < 10; ++i) {
        det.detect(-40.0f);
    }

    // Sudden jump — should trigger onset
    bool onset = det.detect(-20.0f); // +20dB rise >> 6dB threshold
    std::cout << "onset on energy spike: " << onset << "\n";
    assert(onset);

    // Cooldown — should NOT fire even with another jump
    bool duringCooldown = det.detect(-10.0f);
    std::cout << "onset during cooldown: " << duringCooldown << "\n";
    assert(!duringCooldown);

    // After cooldown expires, can fire again
    for (int i = 0; i < 4; ++i) {
        det.detect(-40.0f);
    }
    bool afterCooldown = det.detect(-20.0f);
    std::cout << "onset after cooldown: " << afterCooldown << "\n";
    assert(afterCooldown);
}

// --- M8: Adaptive Frame Size & Overlap tests ---

static void testSlidingWindowOverlap() {
    // With 75% overlap on a 2048 frame, hopSize should be 512.
    // Feed a continuous 440 Hz sine and verify we get results at hop rate.
    constexpr float sr = 48000.0f;
    constexpr int frameSize = 2048;
    constexpr float overlap = 0.75f;

    int callbackCount = 0;
    PitchResult lastResult;

    AudioFrameDispatcher dispatcher(frameSize, sr,
        [&](const PitchResult& r) {
            callbackCount++;
            lastResult = r;
        },
        overlap
    );

    assert(dispatcher.hopSize() == 512);  // 2048 * (1 - 0.75) = 512
    assert(dispatcher.frameSize() == 2048);

    // Generate enough audio to fill multiple hops
    // First frame needs 2048, then each subsequent needs 512
    constexpr int totalSamples = 2048 + 512 * 7; // 1 first + 7 hops = 8 callbacks
    auto signal = generateSine(440.0f, sr, totalSamples);

    dispatcher.start();
    dispatcher.push(signal.data(), totalSamples);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dispatcher.stop();

    std::cout << "sliding window overlap 75%: callbacks=" << callbackCount
              << " lastFreq=" << lastResult.frequency << "\n";

    // Should have received multiple callbacks (first frame + hops)
    assert(callbackCount >= 4); // at least some overlap callbacks fired
}

static void testSlidingWindowNoOverlap() {
    // With 0% overlap, behavior should be identical to the old implementation
    constexpr float sr = 48000.0f;
    constexpr int frameSize = 2048;

    int callbackCount = 0;

    AudioFrameDispatcher dispatcher(frameSize, sr,
        [&](const PitchResult& r) {
            callbackCount++;
        },
        0.0f
    );

    assert(dispatcher.hopSize() == 2048); // no overlap → hop = frame

    auto signal = generateSine(440.0f, sr, 2048 * 4);

    dispatcher.start();
    dispatcher.push(signal.data(), 2048 * 4);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dispatcher.stop();

    std::cout << "sliding window no overlap: callbacks=" << callbackCount << "\n";
    assert(callbackCount == 4); // exactly 4 full frames
}

static void testAdaptiveFrameSizeBass() {
    // Bass instrument should use 4096 frame for E1 (41 Hz) detection
    constexpr float sr = 48000.0f;
    constexpr int frameSize = 4096;
    constexpr float e1Freq = 41.2f;

    auto signal = generateSine(e1Freq, sr, frameSize * 8);

    TunerEngine engine(sr, frameSize);
    engine.setInstrument("bass");

    PitchResult result;
    for (int f = 0; f < 8; ++f) {
        result = engine.process(signal.data() + f * frameSize, frameSize);
    }

    std::cout << "adaptive bass E1 (41.2 Hz): hasPitch=" << result.hasPitch
              << " freq=" << result.frequency
              << " note=" << result.noteName << result.octave
              << " conf=" << result.confidence << "\n";

    assert(result.hasPitch);
    assertNear(result.frequency, e1Freq, e1Freq * 0.02f); // within 2%
}

static void testInstrumentRecommendedFrameSize() {
    assert(instrumentRecommendedFrameSize("bass") == 4096);
    assert(instrumentRecommendedFrameSize("cello") == 4096);
    assert(instrumentRecommendedFrameSize("guitar") == 2048);
    assert(instrumentRecommendedFrameSize("ukulele") == 2048);
    assert(instrumentRecommendedFrameSize("violin") == 2048);
    assert(instrumentRecommendedFrameSize("chromatic") == 2048);
    assert(instrumentRecommendedFrameSize("unknown") == 2048);
    std::cout << "instrumentRecommendedFrameSize: all correct\n";
}

static void testDispatcherReconfigure() {
    constexpr float sr = 48000.0f;
    int callbackCount = 0;

    AudioFrameDispatcher dispatcher(2048, sr,
        [&](const PitchResult& r) {
            callbackCount++;
        },
        0.0f
    );

    assert(dispatcher.frameSize() == 2048);

    // Reconfigure to 4096
    dispatcher.reconfigure(4096, sr);
    assert(dispatcher.frameSize() == 4096);
    assert(dispatcher.hopSize() == 4096); // overlap still 0

    // Push audio and verify it processes with new frame size
    auto signal = generateSine(82.41f, sr, 4096 * 2);
    dispatcher.start();
    dispatcher.push(signal.data(), 4096 * 2);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    dispatcher.stop();

    std::cout << "dispatcher reconfigure 2048→4096: callbacks=" << callbackCount << "\n";
    assert(callbackCount == 2);
}

static void testSetOverlapRatio() {
    constexpr float sr = 48000.0f;

    AudioFrameDispatcher dispatcher(2048, sr,
        [](const PitchResult&) {},
        0.0f
    );

    assert(dispatcher.hopSize() == 2048);

    dispatcher.setOverlapRatio(0.5f);
    assert(dispatcher.hopSize() == 1024);

    dispatcher.setOverlapRatio(0.75f);
    assert(dispatcher.hopSize() == 512);

    // Clamp to max 0.75
    dispatcher.setOverlapRatio(0.9f);
    assert(dispatcher.hopSize() == 512); // clamped to 0.75

    // Clamp to min 0.0
    dispatcher.setOverlapRatio(-0.5f);
    assert(dispatcher.hopSize() == 2048); // clamped to 0.0

    std::cout << "setOverlapRatio: all correct\n";
}

// --- SNR weighting ---

static float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

static float rmsOf(const float* data, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += data[i] * data[i];
    return std::sqrt(sum / static_cast<float>(n));
}

// White noise at the given RMS level (dBFS).
static std::vector<float> generateNoise(int sampleCount, float rmsDb, uint32_t seed = 0xC0FFEEu) {
    std::vector<float> buffer(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        buffer[i] = static_cast<float>(static_cast<int32_t>(seed)) / 2147483648.0f;
    }
    const float scale = dbToLinear(rmsDb) / rmsOf(buffer.data(), sampleCount);
    for (auto& s : buffer) s *= scale;
    return buffer;
}

static void testSnrEstimator() {
    // Weight ramp: nothing below 3 dB, full trust from 12 dB.
    assertNear(SnrEstimator::snrToWeight(0.0f),  0.0f, 1e-6f);
    assertNear(SnrEstimator::snrToWeight(3.0f),  0.0f, 1e-6f);
    assertNear(SnrEstimator::snrToWeight(7.5f),  0.5f, 1e-6f);
    assertNear(SnrEstimator::snrToWeight(12.0f), 1.0f, 1e-6f);
    assertNear(SnrEstimator::snrToWeight(40.0f), 1.0f, 1e-6f);

    // A noisy room: unpitched frames at -45 dBFS must raise the floor there,
    // so the same level no longer counts as signal.
    SnrEstimator noisy;
    for (int i = 0; i < 200; ++i) noisy.update(dbToLinear(-45.0f), false);
    const float roomSnr = noisy.update(dbToLinear(-45.0f), false);
    assert(roomSnr < 1.0f);
    const float noteSnr = noisy.update(dbToLinear(-25.0f), true);
    assertNear(noteSnr, 20.0f, 1.0f);

    // A quiet room: the floor follows the silence down quickly...
    SnrEstimator quiet;
    for (int i = 0; i < 30; ++i) quiet.update(dbToLinear(-85.0f), false);
    const float silenceSnr = quiet.update(dbToLinear(-85.0f), false);
    assert(silenceSnr < 1.0f);

    // ...and a held note (~5 s of pitched frames) barely moves it.
    float heldSnr = 0.0f;
    for (int i = 0; i < 250; ++i) heldSnr = quiet.update(dbToLinear(-50.0f), true);

    std::cout << "snr estimator: room=" << roomSnr
              << " note=" << noteSnr
              << " silence=" << silenceSnr
              << " held=" << heldSnr << "\n";

    assert(heldSnr > 25.0f);
}


static void testPipelineDistantGuitar() {
    // A string plucked at arm's length reaches the phone mic around -50 dBFS
    // (no AGC in measurement mode), well over the default -70 dB gate, in a
    // quiet room at about -75 dBFS. It must still be tuned.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;
    constexpr int   quietFrames = 20;
    constexpr int   noteFrames  = 8;

    auto noise = generateNoise(n * (quietFrames + noteFrames), -75.0f);
    auto tone  = generateHarmonicTone(110.0f, sr, n * noteFrames); // A2
    const float toneScale = dbToLinear(-50.0f) / rmsOf(tone.data(), static_cast<int>(tone.size()));

    std::vector<float> input = noise;
    for (size_t i = 0; i < tone.size(); ++i) {
        input[quietFrames * n + i] += tone[i] * toneScale;
    }

    TunerEngine engine(sr, n);
    engine.setInstrument("guitar");

    PitchResult result;
    for (int f = 0; f < quietFrames + noteFrames; ++f) {
        result = engine.process(input.data() + f * n, n);
    }

    std::cout << "distant guitar A2 @ -50 dBFS: hasPitch=" << result.hasPitch
              << " note=" << result.noteName << result.octave
              << " conf=" << result.confidence
              << " rms=" << result.rmsDb << "\n";

    assert(result.hasPitch);
    assert(result.noteName == "A");
    assert(result.octave == 2);
}

static void testPipelineRejectsToneBuriedInNoise() {
    // Loud room noise that passes the gate: after the floor has learned it, a
    // tone only a few dB above it must not be reported.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;
    constexpr int   noiseFrames = 150;
    constexpr int   noteFrames  = 8;

    auto input = generateNoise(n * (noiseFrames + noteFrames), -40.0f);
    auto tone  = generateHarmonicTone(110.0f, sr, n * noteFrames);
    const float toneScale = dbToLinear(-40.0f) / rmsOf(tone.data(), static_cast<int>(tone.size()));
    for (size_t i = 0; i < tone.size(); ++i) {
        input[noiseFrames * n + i] += tone[i] * toneScale;
    }

    TunerEngine engine(sr, n);
    engine.setInstrument("guitar");

    int pitched = 0;
    for (int f = 0; f < noiseFrames + noteFrames; ++f) {
        auto r = engine.process(input.data() + f * n, n);
        if (f >= noiseFrames && r.hasPitch) ++pitched;
    }

    std::cout << "tone buried in noise: pitched frames=" << pitched << "/" << noteFrames << "\n";

    assert(pitched == 0);
}

static void testPipelineQuietTailStillTuned() {
    // The tail of a ringing string: a G3 at -62 dBFS (13 dB over a -75 dBFS
    // room). Noise alone must stay silent, and the note must keep the needle.
    // Fails with a -55 dB gate (note gated) and with the 6→18 dB SNR ramp
    // (weight 0.6 → confidence under the threshold).
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr int   noiseFrames = 30;
    constexpr int   noteFrames  = 12;

    auto input = generateNoise(n * (noiseFrames + noteFrames), -75.0f);
    auto tone  = generateHarmonicTone(196.0f, sr, n * noteFrames); // G3
    const float toneScale = dbToLinear(-62.0f) / rmsOf(tone.data(), static_cast<int>(tone.size()));
    for (size_t i = 0; i < tone.size(); ++i) {
        input[noiseFrames * n + i] += tone[i] * toneScale;
    }

    TunerEngine engine(sr, n);
    engine.setInstrument("guitar");

    int noisePitched = 0;
    int notePitched = 0;
    PitchResult last;
    for (int f = 0; f < noiseFrames + noteFrames; ++f) {
        last = engine.process(input.data() + f * n, n);
        if (f < noiseFrames && last.hasPitch) ++noisePitched;
        if (f >= noiseFrames && last.hasPitch) ++notePitched;
    }

    std::cout << "quiet tail G3 @ -62 dBFS: noise-frame pitches=" << noisePitched
              << " note frames pitched=" << notePitched << "/" << noteFrames
              << " last=" << last.noteName << last.octave << "\n";

    assert(noisePitched == 0);
    assert(notePitched >= 8);
    assert(last.hasPitch && last.noteName == "G" && last.octave == 3);
}

static void testPyinMultiplesDoNotSplitConfidence() {
    // A clean periodic tone also dips at 2T, 3T... When the winner's mass is
    // compared with the total, those multiples used to dilute the confidence of
    // a perfectly good G4 at 8 dB SNR. Fails with the old pYIN.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr int   trials = 20;

    auto tone = generateHarmonicTone(392.0f, sr, n);
    const float toneScale = dbToLinear(-32.0f) / rmsOf(tone.data(), n);
    for (auto& v : tone) v *= toneScale;

    int confident = 0;
    float minConf = 1.0f;
    for (int seed = 0; seed < trials; ++seed) {
        auto frame = generateNoise(n, -40.0f, 0xC0FFEEu + 7919u * static_cast<uint32_t>(seed));
        for (int i = 0; i < n; ++i) frame[i] += tone[i];

        PyinPitchDetector pyin(sr, n);
        auto r = pyin.detect(frame.data(), n, sr);

        assert(r.voiced);
        assert(std::fabs(r.frequency - 392.0f) / 392.0f < 0.01f);
        if (r.confidence >= 0.75f) ++confident;
        minConf = std::min(minConf, r.confidence);
    }

    std::cout << "pyin G4 @ 8 dB SNR: confident " << confident << "/" << trials
              << " min conf=" << minConf << "\n";

    assert(confident >= 18);
}

// --- Note hold -------------------------------------------------------------

// One frame of a harmonic tone at toneDb (RMS dBFS) over -75 dBFS white noise.
// Sample position of the stream the frame helpers below cut their frames
// from. Consecutive frames of one note must continue each other's phase, as
// frames of a real stream do: the engine measures a note's partials across
// frame boundaries, and a tone restarted at phase 0 every frame is a
// different sound.
static long gStreamPosition = 0;

// `sampleCount` samples of the harmonic tone, starting at gStreamPosition.
static std::vector<float> nextHarmonicTone(float fundamental, int sampleCount) {
    std::vector<float> buffer(sampleCount, 0.0f);
    for (int harmonic = 1; harmonic <= 5; ++harmonic) {
        const float amplitude = 0.5f / static_cast<float>(harmonic);
        const double step = 2.0 * 3.14159265358979323846 * fundamental * harmonic / 48000.0;
        for (int i = 0; i < sampleCount; ++i) {
            buffer[i] += amplitude * static_cast<float>(std::sin(step * static_cast<double>(gStreamPosition + i)));
        }
    }
    gStreamPosition += sampleCount;
    return buffer;
}

static std::vector<float> toneOverRoom(float hz, float toneDb, uint32_t seed, int n = 2048) {
    auto frame = generateNoise(n, -75.0f, seed);
    auto tone  = nextHarmonicTone(hz, n);
    const float k = dbToLinear(toneDb) / rmsOf(tone.data(), n);
    for (int i = 0; i < n; ++i) frame[i] += tone[i] * k;
    return frame;
}

// Tone whose level glides linearly in dB across the frame, over -75 dBFS noise.
// Real streams have no level steps; independent frames with a 27 dB step would
// excite the HPF's carried-over state and distort the next frame.
static std::vector<float> rampFrame(float hz, float fromDb, float toDb, uint32_t seed, int n = 2048) {
    auto frame = generateNoise(n, -75.0f, seed);
    auto tone  = nextHarmonicTone(hz, n);
    const float unit = 1.0f / rmsOf(tone.data(), n);
    for (int i = 0; i < n; ++i) {
        const float db = fromDb + (toDb - fromDb) * static_cast<float>(i) / static_cast<float>(n);
        frame[i] += tone[i] * unit * dbToLinear(db);
    }
    return frame;
}

// Fresh engine whose SNR floor has learned the room, locked onto `hz`, then
// faded from -40 to -67 dBFS in one frame, ready for weak (-67 dBFS) frames.
static void lockNote(TunerEngine& engine, float hz, int n = 2048) {
    gStreamPosition = 0;
    for (int f = 0; f < 30; ++f) {
        auto noise = generateNoise(n, -75.0f, 0xA000u + static_cast<uint32_t>(f));
        engine.process(noise.data(), n);
    }
    for (int f = 0; f < 8; ++f) {
        auto frame = toneOverRoom(hz, -40.0f, 0xB000u + static_cast<uint32_t>(f));
        engine.process(frame.data(), n);
    }
    auto fade = rampFrame(hz, -40.0f, -67.0f, 0xB100u);
    engine.process(fade.data(), n);
}

static void testNoteHoldKeepsDecayingNote() {
    // G3 decaying exponentially (linear in dB) from -35 to -68 dBFS over ~40
    // frames. Its confidence sinks below 0.75 well before the note is gone.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr int   noiseFrames = 10;
    constexpr int   noteFrames  = 40;

    auto noise = generateNoise(n * (noiseFrames + noteFrames), -75.0f);
    auto tone  = generateHarmonicTone(196.0f, sr, n * noteFrames);
    const float unit = 1.0f / rmsOf(tone.data(), static_cast<int>(tone.size()));
    std::vector<float> input = noise;
    for (int i = 0; i < n * noteFrames; ++i) {
        const float levelDb = -35.0f - 33.0f * static_cast<float>(i) / static_cast<float>(n * noteFrames);
        input[noiseFrames * n + i] += tone[i] * unit * dbToLinear(levelDb);
    }

    auto run = [&](bool hold, int& wrongNote) {
        TunerEngine engine(sr, n);
        engine.setInstrument("guitar");
        Pipeline::NoteHold cfg;
        cfg.enabled = hold;
        engine.setNoteHold(cfg);
        int pitched = 0;
        wrongNote = 0;
        for (int f = noiseFrames; f < noiseFrames + noteFrames; ++f) {
            auto r = engine.process(input.data() + f * n, n);
            if (!r.hasPitch) continue;
            ++pitched;
            if (r.noteName != "G" || r.octave != 3) ++wrongNote;
        }
        return pitched;
    };

    int wrongOn = 0, wrongOff = 0;
    const int withHold    = run(true, wrongOn);
    const int withoutHold = run(false, wrongOff);

    std::cout << "note hold, decaying G3: pitched frames hold=" << withHold
              << " no hold=" << withoutHold << " (wrong note " << wrongOn << ")\n";

    // A note in hand is now followed by its partials for as long as they
    // can be measured, with or without the hold, so the hold no longer adds
    // frames here. It must not lose any.
    assert(withHold >= withoutHold);
    assert(withHold >= 34);
    assert(wrongOn == 0);
}

static void testNoteHoldDoesNotStartOrJump() {
    constexpr int n = 2048;

    // (a) Weak tone on a fresh engine: confident enough to hold a note, not to
    // start one. It must stay silent, and be rejected for confidence only.
    {
        TunerEngine engine(48000.0f, n);
        engine.setInstrument("guitar");
        for (int f = 0; f < 30; ++f) {
            auto noise = generateNoise(n, -75.0f, 0xA000u + static_cast<uint32_t>(f));
            engine.process(noise.data(), n);
        }
        int pitched = 0;
        PitchResult r;
        for (int f = 0; f < 12; ++f) {
            auto frame = toneOverRoom(196.0f, -67.0f, 0xC000u + static_cast<uint32_t>(f));
            r = engine.process(frame.data(), n);
            if (r.hasPitch) ++pitched;
            const float weighted = r.detectorConfidence * SnrEstimator::snrToWeight(r.snrDb);
            assert(r.stage == PitchStage::LowConfidence);
            assert(weighted >= 0.4f && weighted < 0.75f);
        }
        std::cout << "note hold, fresh engine weak tone: pitched=" << pitched << "/12\n";
        assert(pitched == 0);
    }

    // (b) Locked on A2, then a weak detection a semitone up (A#2): neither held
    // as A2 nor reported.
    {
        TunerEngine engine(48000.0f, n);
        engine.setInstrument("guitar");
        lockNote(engine, 110.0f);
        auto frame = toneOverRoom(116.54f, -67.0f, 0xD000u);
        auto r = engine.process(frame.data(), n);
        std::cout << "note hold, weak A#2 after A2: hasPitch=" << r.hasPitch
                  << " stage=" << int(r.stage) << "\n";
        assert(!r.hasPitch);
        assert(r.stage == PitchStage::LowConfidence);
    }
}

static void testNoteHoldReleases() {
    constexpr int n = 2048;
    auto weakG3 = [&](uint32_t seed) { return toneOverRoom(196.0f, -67.0f, seed); };

    // Control: right after the lock a weak frame IS held.
    {
        TunerEngine engine(48000.0f, n);
        engine.setInstrument("guitar");
        lockNote(engine, 196.0f);
        auto f = weakG3(0xF001u);
        auto r = engine.process(f.data(), n);
        assert(r.hasPitch && r.noteName == "G" && r.octave == 3);
    }

    // One missed frame (maxMissedFrames = 2) keeps the hold; three drop it.
    for (int misses = 1; misses <= 3; ++misses) {
        TunerEngine engine(48000.0f, n);
        engine.setInstrument("guitar");
        lockNote(engine, 196.0f);
        for (int m = 0; m < misses; ++m) {
            auto noise = generateNoise(n, -62.0f, 0xE000u + static_cast<uint32_t>(m));
            auto r = engine.process(noise.data(), n);
            gStreamPosition += n; // the string keeps its phase while unheard
            assert(!r.hasPitch); // noise alone is never a note
        }
        auto f = weakG3(0xF002u);
        auto r = engine.process(f.data(), n);
        std::cout << "note hold release: " << misses << " missed frames, weak G3 hasPitch="
                  << r.hasPitch << "\n";
        assert(r.hasPitch == (misses <= 2));
    }

    // A gated frame releases it at once.
    {
        TunerEngine engine(48000.0f, n);
        engine.setInstrument("guitar");
        lockNote(engine, 196.0f);
        // Room noise under the gate: leaves the SNR floor where it was.
        auto room = generateNoise(n, -75.0f, 0xE100u);
        auto g = engine.process(room.data(), n);
        assert(g.stage == PitchStage::Gated);
        auto f = weakG3(0xF003u);
        auto r = engine.process(f.data(), n);
        assert(!r.hasPitch);
    }
}

static void testPyinRealStringNoOctaveUp() {
    // Real recorded A#3 (233 Hz) whose strong 2nd harmonic used to make pYIN
    // report 469 Hz with confidence > 0.9. Fails with the octave-up repair removed.
    constexpr float sr = 44100.0f;
    constexpr int   n  = 2048;
    constexpr int   frames = 3;

    std::vector<float> data(static_cast<size_t>(n) * frames);
    FILE* f = std::fopen(TUNER_TEST_DATA_DIR "/strat_Asharp3_frames.f32", "rb");
    assert(f && "missing tests/data/strat_Asharp3_frames.f32");
    const size_t got = std::fread(data.data(), sizeof(float), data.size(), f);
    std::fclose(f);
    assert(got == data.size());

    for (int k = 0; k < frames; ++k) {
        PyinPitchDetector pyin(sr, n);
        auto r = pyin.detect(data.data() + static_cast<size_t>(k) * n, n, sr);
        std::cout << "pyin real A#3 frame " << k << ": f=" << r.frequency
                  << " conf=" << r.confidence << "\n";
        assert(r.voiced);
        assert(std::fabs(r.frequency - 233.08f) / 233.08f < 0.01f);
    }
}

// Appends `frames` frames of a harmonic tone at `hz` to `out`.
static void appendTone(std::vector<float>& out, float hz, float sr, int n, int frames) {
    auto tone = generateHarmonicTone(hz, sr, n * frames);
    for (float& v : tone) v *= 0.3f;
    out.insert(out.end(), tone.begin(), tone.end());
}

static void testNewNoteAfterSilenceIsNotThePreviousOne() {
    // A2, a pause, then D3. The median and EMA used to survive the pause, so
    // the first D3 frames were reported as A2 and then as B2 and C3.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr int   firstFrames = 20, silentFrames = 8, secondFrames = 12;

    std::vector<float> input;
    appendTone(input, 110.0f, sr, n, firstFrames);
    input.resize(input.size() + static_cast<size_t>(n * silentFrames), 0.0f);
    appendTone(input, 146.83f, sr, n, secondFrames);

    TunerEngine engine(sr, n);
    engine.setInstrument("guitar");

    const int onset = firstFrames + silentFrames;
    int pitched = 0, wrongNote = 0, firstPitched = -1;
    for (int f = 0; f < onset + secondFrames; ++f) {
        auto r = engine.process(input.data() + f * n, n);
        if (f < onset || !r.hasPitch) continue;
        if (firstPitched < 0) firstPitched = f - onset;
        ++pitched;
        if (r.noteName != "D" || r.octave != 3) ++wrongNote;
    }

    std::cout << "A2, silence, D3: pitched=" << pitched << " wrong note=" << wrongNote
              << " first pitched frame=" << firstPitched << "\n";

    assert(wrongNote == 0);
    // The third frame at the latest: a new note is shown once two
    // measurements agree.
    assert(firstPitched >= 0 && firstPitched <= 2);
}

static void testNoteChangeReportsNoPhantomNotes() {
    // A2 straight into D3, no pause. The EMA used to blend the two while the
    // change was being confirmed, reporting B2 (+192 cents) and C3 on the way.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr int   firstFrames = 20, secondFrames = 12;

    std::vector<float> input;
    appendTone(input, 110.0f, sr, n, firstFrames);
    appendTone(input, 146.83f, sr, n, secondFrames);

    TunerEngine engine(sr, n);
    engine.setInstrument("guitar");

    int phantom = 0, reachedD3 = 0;
    for (int f = 0; f < firstFrames + secondFrames; ++f) {
        auto r = engine.process(input.data() + f * n, n);
        if (f < firstFrames || !r.hasPitch) continue;
        const bool isA2 = r.noteName == "A" && r.octave == 2;
        const bool isD3 = r.noteName == "D" && r.octave == 3;
        if (!isA2 && !isD3) ++phantom;
        if (isD3) {
            ++reachedD3;
            assert(std::fabs(r.cents) < 5.0f);
        }
    }

    std::cout << "A2 into D3: phantom notes=" << phantom << " D3 frames=" << reachedD3 << "\n";

    assert(phantom == 0);
    assert(reachedD3 > 0);
}

static void testCentsFollowA4() {
    // With A4 = 442, a 442 Hz tone is A4 at 0 cents. The post-processor
    // measured against a hard-coded 440 and its cents replaced the mapper's,
    // so this read +7.86.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr int   frames = 12;

    auto input = generateHarmonicTone(442.0f, sr, n * frames);

    TunerEngine engine(sr, n);
    engine.setA4(442.0f);

    PitchResult last;
    for (int f = 0; f < frames; ++f) {
        auto r = engine.process(input.data() + f * n, n);
        if (r.hasPitch) last = r;
    }

    std::cout << "A4=442, 442 Hz tone: " << last.noteName << last.octave
              << " cents=" << last.cents << "\n";

    assert(last.hasPitch);
    assert(last.noteName == "A" && last.octave == 4);
    assertNear(last.cents, 0.0f, 1.0f);
}

static void testJustTemperamentCentsReachTheResult() {
    // Same override: the just-intonation offset the mapper applies never
    // reached the result. E is 13.69 cents flat of equal temperament in just
    // intonation, so an equal-tempered E4 reads about +13.7.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr int   frames = 12;

    auto input = generateHarmonicTone(329.63f, sr, n * frames);

    TunerEngine engine(sr, n);
    engine.setTemperament("just");

    PitchResult last;
    for (int f = 0; f < frames; ++f) {
        auto r = engine.process(input.data() + f * n, n);
        if (r.hasPitch) last = r;
    }

    std::cout << "just temperament, equal E4: cents=" << last.cents << "\n";

    assert(last.hasPitch);
    assertNear(last.cents, 13.69f, 1.5f);
}

static void testOverlapDoesNotBiasLowStrings() {
    // Overlapping frames ran the stateful high-pass filter over the shared
    // samples again, starting each frame from the state the previous frame's
    // END had left. On a decaying low E that read several cents sharp at
    // 50 % overlap. Passing the hop lets the pipeline filter each sample once.
    constexpr float sr  = 48000.0f;
    constexpr int   n   = 2048;
    constexpr int   hop = 1024;
    constexpr float hz  = 82.407f;
    constexpr int   total = 96000; // 2 s

    auto input = generateHarmonicTone(hz, sr, total);
    auto noise = generateNoise(total, -60.0f);
    for (int i = 0; i < total; ++i) {
        const float decay = std::exp(-1.5f * static_cast<float>(i) / sr);
        input[i] = 0.4f * decay * input[i] + noise[i];
    }

    auto meanErrorCents = [&](bool passHop) {
        TunerEngine engine(sr, n);
        engine.setFrequencyRange(55.0f, 1320.0f);
        engine.setHpfCutoff(45.0f);
        double sum = 0.0;
        int count = 0;
        for (int start = 0; start + n <= total; start += hop) {
            auto r = (passHop && start > 0)
                ? engine.process(input.data() + start, n, hop)
                : engine.process(input.data() + start, n);
            const float t = static_cast<float>(start + n) / sr;
            if (!r.hasPitch || t < 0.3f || t > 1.5f) continue;
            sum += 1200.0 * std::log2(r.frequency / hz);
            ++count;
        }
        assert(count > 20);
        return static_cast<float>(sum / count);
    };

    const float withHop    = meanErrorCents(true);
    const float withoutHop = meanErrorCents(false);

    std::cout << "E2 at 50% overlap: mean error with hop=" << withHop
              << " cents, refiltering the overlap=" << withoutHop << " cents\n";

    assert(std::fabs(withHop) < 0.5f);
}

static void testPyinNoSharpBiasInNoise() {
    // Noise splits a CMND dip into several local minima and the threshold rule
    // credits the first, i.e. the shortest lag. Averaged over many noise
    // realisations a low E at 14 dB SNR read about 7 cents sharp.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 2048;
    constexpr float hz = 82.407f;
    constexpr int   frames = 20;
    constexpr int   seeds  = 24;

    auto tone = generateHarmonicTone(hz, sr, n * frames);
    const float toneRms = rmsOf(tone.data(), static_cast<int>(tone.size()));
    const float noiseDb = 20.0f * std::log10(toneRms) - 14.0f;

    double sum = 0.0;
    int count = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        auto input = generateNoise(n * frames, noiseDb, 0x1234u + static_cast<uint32_t>(seed));
        for (size_t i = 0; i < input.size(); ++i) input[i] += tone[i];

        PyinPitchDetector detector(sr, n);
        detector.setFrequencyRange(55.0f, 1320.0f);
        for (int f = 0; f < frames; ++f) {
            auto r = detector.detect(input.data() + f * n, n, sr);
            if (!r.voiced) continue;
            const double cents = 1200.0 * std::log2(r.frequency / hz);
            if (std::fabs(cents) > 100.0) continue; // octave errors are not this test
            sum += cents;
            ++count;
        }
    }
    const double mean = sum / count;

    std::cout << "pYIN E2 at 14 dB SNR: mean error " << mean << " cents over "
              << count << " frames\n";

    assert(count > seeds * frames / 2);
    assert(std::fabs(mean) < 1.5);
}

// A string: partial k at k * f0 * sqrt(1 + B k^2) with amplitude amp[k-1],
// added to `out` from `start` on, decaying slowly.
static void addStiffString(std::vector<float>& out, int start, double f0, double stiffness,
                           const std::vector<double>& amplitudes, double level) {
    constexpr double sr = 48000.0;
    for (size_t k = 1; k <= amplitudes.size(); ++k) {
        const double hz = k * f0 * std::sqrt(1.0 + stiffness * k * k);
        if (hz > 0.45 * sr) break;
        const double step = 2.0 * 3.14159265358979323846 * hz / sr;
        for (size_t i = static_cast<size_t>(start); i < out.size(); ++i) {
            const double t = static_cast<double>(i - static_cast<size_t>(start)) / sr;
            out[i] += static_cast<float>(level * amplitudes[k - 1] * std::exp(-1.0 * t)
                                         * std::sin(step * static_cast<double>(i - static_cast<size_t>(start)) + 0.3 * k));
        }
    }
}

// Runs `input` through an engine and returns the mean error in cents against
// `hz` over the readings between `fromSeconds` and the end, with their count
// and the number of readings more than 50 cents away.
struct ReadingStats { double meanCents; int count; int wrongNote; };
static ReadingStats readStream(const std::vector<float>& input, double hz, double fromSeconds,
                               int n = 2048, float minHz = 55.0f, float maxHz = 1320.0f) {
    TunerEngine engine(48000.0f, n);
    engine.setFrequencyRange(minHz, maxHz);
    engine.setHpfCutoff(45.0f);
    ReadingStats stats{0.0, 0, 0};
    double sum = 0.0;
    for (size_t p = 0; p + static_cast<size_t>(n) <= input.size(); p += static_cast<size_t>(n)) {
        auto r = engine.process(input.data() + p, n);
        if (!r.hasPitch || static_cast<double>(p + n) / 48000.0 < fromSeconds) continue;
        const double cents = 1200.0 * std::log2(r.frequency / hz);
        if (std::fabs(cents) > 50.0) { ++stats.wrongNote; continue; }
        sum += cents;
        ++stats.count;
    }
    stats.meanCents = stats.count ? sum / stats.count : 0.0;
    return stats;
}

static const std::vector<double> kPluckedAmplitudes = {1.0, 0.5, 0.33, 0.25, 0.2, 0.17, 0.14, 0.12};

static void testStiffStringReadsItsFundamental() {
    // A wound low E with stiffness B = 3e-4: its partials sit progressively
    // above exact multiples, and a period estimate of the whole wave read
    // about 5.5 cents sharp.
    std::vector<float> input = generateNoise(48000 * 2, -70.0f);
    addStiffString(input, 24000, 82.407, 3.0e-4, kPluckedAmplitudes, 0.1);
    const auto stats = readStream(input, 82.407, 0.8);
    std::cout << "stiff E2 (B=3e-4): mean " << stats.meanCents << " cents over " << stats.count << " readings\n";
    assert(stats.count > 20 && stats.wrongNote == 0);
    assert(std::fabs(stats.meanCents) < 0.3);
}

static void testRingingNeighbourDoesNotMoveTheReading() {
    // A2 plucked while the low E still rings 10 dB under it. The period
    // estimate was pulled about 5 cents flat, with a fixed sign.
    std::vector<float> input = generateNoise(48000 * 2, -70.0f);
    addStiffString(input, 4800, 82.407, 0.0, kPluckedAmplitudes, 0.1 * 0.316);
    addStiffString(input, 28800, 110.0, 0.0, kPluckedAmplitudes, 0.1);
    const auto stats = readStream(input, 110.0, 0.9);
    std::cout << "A2 over a ringing E2 (-10 dB): mean " << stats.meanCents << " cents over " << stats.count << " readings\n";
    assert(stats.count > 20 && stats.wrongNote == 0);
    assert(std::fabs(stats.meanCents) < 0.3);
}

static void testNoteOverRingingFourthIsRead() {
    // E4 plucked over a B3 ringing 10 dB under it. Together they repeat at
    // the period of E2, which is what the period detector reports, with too
    // little confidence to show anything. Nothing sounds at E2 itself.
    std::vector<float> input = generateNoise(48000 * 2, -70.0f);
    addStiffString(input, 4800, 246.942, 0.0, kPluckedAmplitudes, 0.1 * 0.316);
    addStiffString(input, 28800, 329.628, 0.0, kPluckedAmplitudes, 0.1);
    const auto stats = readStream(input, 329.628, 0.9);
    std::cout << "E4 over a ringing B3 (-10 dB): mean " << stats.meanCents << " cents over " << stats.count << " readings\n";
    assert(stats.count > 20 && stats.wrongNote == 0);
    assert(std::fabs(stats.meanCents) < 0.3);
}

static void testPulledFundamentalDoesNotOutvoteTheSeries() {
    // On a phone recording of a guitar the fundamental of the B string stood
    // 20 dB over the next partials and 10 cents over the series for half a
    // second (the top resonates there); G was the same, 8 cents under, for
    // the whole note. The partials above agree on where the string is.
    constexpr double sr = 48000.0, twoPi = 6.283185307179586;
    constexpr double f0 = 246.94;
    std::vector<float> input = generateNoise(48000 * 2, -70.0f);
    const double amp[] = {1.0, 0.1, 0.12, 0.03, 0.05, 0.04};
    for (size_t i = 24000; i < input.size(); ++i) {
        const double t = (i - 24000) / sr;
        double v = 0.0;
        for (int k = 1; k <= 6; ++k) {
            const double f = k * f0 * (k == 1 ? std::pow(2.0, 10.0 / 1200.0) : 1.0);
            v += amp[k - 1] * std::exp(-1.5 * t) * std::sin(twoPi * f * t + 0.4 * k);
        }
        input[i] += static_cast<float>(0.1 * v);
    }
    const auto stats = readStream(input, f0, 0.8);
    std::cout << "B3, fundamental 10 cents over its series: mean " << stats.meanCents
              << " cents over " << stats.count << " readings\n";
    assert(stats.count > 20 && stats.wrongNote == 0);
    assert(std::fabs(stats.meanCents) < 1.0);
}

static void testBodyThumpBelowTheNoteIsNotItsFundamental() {
    // A nylon A4 came with a thump of the guitar's body near 222 Hz, a third
    // of a semitone over half the note. The period detector answered with
    // the octave below, and that peak passed for its fundamental: an A3 with
    // no third, fifth or seventh partial.
    constexpr double sr = 48000.0, twoPi = 6.283185307179586;
    std::vector<float> input = generateNoise(48000 * 2, -70.0f);
    const double amp[] = {1.0, 0.3, 0.25, 0.14, 0.06, 0.05};
    for (size_t i = 24000; i < input.size(); ++i) {
        const double t = (i - 24000) / sr;
        double v = 0.25 * std::exp(-2.0 * t) * std::sin(twoPi * 223.0 * t);
        for (int k = 1; k <= 6; ++k) {
            v += amp[k - 1] * std::exp(-(1.0 + 0.4 * k) * t) * std::sin(twoPi * k * 440.0 * t + 0.3 * k);
        }
        input[i] += static_cast<float>(0.1 * v);
    }
    const auto stats = readStream(input, 440.0, 0.8);
    std::cout << "A4 over a body thump at 223 Hz: mean " << stats.meanCents << " cents, wrong note "
              << stats.wrongNote << " of " << (stats.count + stats.wrongNote) << "\n";
    assert(stats.count > 20 && stats.wrongNote == 0);
    assert(std::fabs(stats.meanCents) < 0.5);
}

static void testTurningPegIsFollowed() {
    // A string tuned up by 30 cents in half a second while it rings, then
    // left alone. Smoothing alone trailed it; the reading must stay close
    // while it moves and land on the new pitch when it stops.
    constexpr double sr = 48000.0, twoPi = 6.283185307179586;
    constexpr int n = 2048;
    constexpr double f0 = 196.0, turnAt = 1.0, turnFor = 0.5, turnBy = 30.0;
    std::vector<float> input = generateNoise(48000 * 4, -70.0f);
    const double amp[] = {1.0, 0.5, 0.35, 0.2, 0.15, 0.1};
    double phase = 0.0;
    std::vector<double> truth(input.size(), 0.0);
    for (size_t i = 24000; i < input.size(); ++i) {
        const double t = (i - 24000) / sr;
        const double c = std::clamp((t - turnAt) / turnFor, 0.0, 1.0) * turnBy;
        truth[i] = c;
        phase += twoPi * f0 * std::pow(2.0, c / 1200.0) / sr;
        double v = 0.0;
        for (int k = 1; k <= 6; ++k) v += amp[k - 1] * std::exp(-(0.8 + 0.1 * k) * t) * std::sin(k * phase + 0.3 * k);
        input[i] += static_cast<float>(0.1 * v);
    }
    TunerEngine engine(48000.0f, n);
    engine.setInstrument("guitar");
    double worstMoving = 0.0, worstAfter = 0.0, worstSettled = 0.0;
    int after = 0;
    for (size_t p = 0; p + n <= input.size(); p += n) {
        const auto r = engine.process(input.data() + p, n);
        const double t = (static_cast<double>(p + n) - 24000) / sr;
        if (!r.hasPitch || t < turnAt) continue;
        const double error = std::fabs(1200.0 * std::log2(r.frequency / f0) - truth[p + n - 1]);
        if (t <= turnAt + turnFor + 0.2) worstMoving = std::max(worstMoving, error);
        else if (t <= turnAt + turnFor + 1.2) { worstAfter = std::max(worstAfter, error); ++after; }
        if (t > turnAt + turnFor + 0.5) worstSettled = std::max(worstSettled, error);
    }
    std::cout << "peg turned 30 cents in 0.5 s: worst " << worstMoving << " cents behind while moving, "
              << worstAfter << " after it stopped, " << worstSettled << " half a second later\n";
    assert(after > 15);
    // Carried forward by its trend the reading runs a little past where
    // the peg stopped, for a frame or two.
    assert(worstMoving < 8.0);
    assert(worstAfter < 2.0);
    assert(worstSettled < 0.5);
}

static void testMainsHumIsNotANote() {
    // 50 Hz and its harmonics, steady, with nobody playing: a clean series
    // inside a bass's range.
    constexpr double sr = 48000.0, twoPi = 6.283185307179586;
    constexpr int n = 4096;
    std::vector<float> input = generateNoise(48000 * 3, -70.0f);
    for (size_t i = 0; i < input.size(); ++i) {
        const double t = i / sr;
        input[i] += static_cast<float>(0.01 * (std::sin(twoPi * 50 * t)
            + 0.5 * std::sin(twoPi * 100 * t + 1) + 0.3 * std::sin(twoPi * 150 * t + 2)));
    }
    TunerEngine engine(48000.0f, n);
    engine.setFrequencyRange(30.0f, 400.0f);
    engine.setHpfCutoff(25.0f);
    int pitched = 0;
    for (size_t p = 0; p + n <= input.size(); p += n) {
        if (engine.process(input.data() + p, n).hasPitch) ++pitched;
    }
    std::cout << "mains hum alone, bass range: " << pitched << " pitched frames\n";
    assert(pitched == 0);
}

static void testWeakFundamentalIsNotReadAnOctaveUp() {
    // A string whose even partials carry over 90 % of the energy (a real
    // nylon E4 does). Judged by energy it is an E5; its weak fundamental says
    // otherwise.
    std::vector<float> input = generateNoise(48000 * 2, -70.0f);
    addStiffString(input, 24000, 329.628, 5.0e-5, {0.2, 1.0, 0.1, 0.5, 0.05, 0.3}, 0.1);
    const auto stats = readStream(input, 329.628, 0.8);
    std::cout << "E4 with a weak fundamental: mean " << stats.meanCents << " cents, wrong note "
              << stats.wrongNote << " of " << (stats.count + stats.wrongNote) << "\n";
    assert(stats.count > 20 && stats.wrongNote == 0);
    assert(std::fabs(stats.meanCents) < 0.3);
}

static void testRingingLowStringStaysItself() {
    // A low E as a phone hears it: the second partial 14 dB over the
    // fundamental and outlasting it, in a room with low rumble. Asked afresh
    // every frame, the period detector turned it into an E3 after a few
    // seconds, and a fading B3 into a B1. A note in hand is followed by its
    // own partials until the next pluck.
    constexpr double sr = 48000.0;
    constexpr int    n  = 2048;
    const int total = static_cast<int>(sr * 7.0), start = static_cast<int>(sr * 1.0);
    std::vector<float> input = generateNoise(total, -62.0f);
    // rumble: slow random walk, strong under 200 Hz
    {
        auto white = generateNoise(total, -30.0f, 0xBEEFu);
        double low = 0.0;
        for (int i = 0; i < total; ++i) { low = 0.995 * low + 0.005 * white[i]; input[i] += static_cast<float>(4.0 * low); }
    }
    const double f0 = 82.407;
    const double amplitude[6] = {0.2, 1.0, 0.08, 0.03, 0.09, 0.04};
    const double decay[6]     = {0.9, 0.35, 0.5, 0.7, 0.8, 0.9};
    for (int k = 1; k <= 6; ++k) {
        const double step = 2.0 * 3.14159265358979323846 * k * f0 * std::sqrt(1.0 + 1.0e-4 * k * k) / sr;
        for (int i = start; i < total; ++i) {
            const double t = (i - start) / sr;
            input[i] += static_cast<float>(0.2 * amplitude[k - 1] * std::exp(-decay[k - 1] * t) * std::sin(step * (i - start) + 0.3 * k));
        }
    }

    TunerEngine engine(48000.0f, n);
    engine.setFrequencyRange(55.0f, 1320.0f);
    engine.setHpfCutoff(45.0f);
    int right = 0, wrong = 0;
    for (int p = 0; p + n <= total; p += n) {
        auto r = engine.process(input.data() + p, n);
        if (!r.hasPitch) continue;
        if (r.noteName == "E" && r.octave == 2) ++right; else ++wrong;
    }
    std::cout << "ringing low E, dominant 2nd partial, in rumble: E2 " << right << " frames, other notes " << wrong << "\n";
    assert(right > 60);
    assert(wrong == 0);
}

int main() {
    testNoteMapper();

    testPyin(82.41f);
    testPyin(110.0f);
    testPyin(146.83f);
    testPyin(196.0f);
    testPyin(246.94f);
    testPyin(329.63f);

    testTunerEngine(82.41f, "E", 2);
    testTunerEngine(110.0f, "A", 2);
    testTunerEngine(146.83f, "D", 3);
    testTunerEngine(196.0f, "G", 3);
    testTunerEngine(246.94f, "B", 3);
    testTunerEngine(329.63f, "E", 4);

    testTunerEngineSilence();
    testTunerEngineQuietSignal();

    // M3 per-detector and fusion tests
    testDetectorOnSine(82.41f,  "E2");
    testDetectorOnSine(110.0f,  "A2");
    testDetectorOnSine(146.83f, "D3");
    testDetectorOnSine(196.0f,  "G3");
    testDetectorOnSine(329.63f, "E4");
    testDetectorOnSine(440.0f,  "A4");
    testCepstrumOnHarmonicTone();
    testFusionDecisions();
    testFusionAgreementOnHarmonicTone();
    testEngineOctaveSafety();

    // M2 DSP hardening tests
    testPipelineCleanSine(82.41f,  "E", 2);
    testPipelineCleanSine(110.0f,  "A", 2);
    testPipelineCleanSine(196.0f,  "G", 3);
    testPipelineCleanSine(329.63f, "E", 4);
    testPipelineCleanSine(440.0f,  "A", 4);
    testPipelineNoisySignal();
    testPipelineHysteresis();
    testInstrumentPreset();
    testOnsetDetector();

    // SNR weighting
    testSnrEstimator();
    testPipelineDistantGuitar();
    testPipelineRejectsToneBuriedInNoise();
    testPipelineQuietTailStillTuned();
    testPyinMultiplesDoNotSplitConfidence();
    testPyinRealStringNoOctaveUp();
    testNoteHoldKeepsDecayingNote();
    testNoteHoldDoesNotStartOrJump();
    testNoteHoldReleases();

    // Note tracking across note changes, A4 reference, overlap
    testNewNoteAfterSilenceIsNotThePreviousOne();
    testNoteChangeReportsNoPhantomNotes();
    testCentsFollowA4();
    testJustTemperamentCentsReachTheResult();
    testOverlapDoesNotBiasLowStrings();
    testPyinNoSharpBiasInNoise();

    // Spectral refinement
    testStiffStringReadsItsFundamental();
    testRingingNeighbourDoesNotMoveTheReading();
    testNoteOverRingingFourthIsRead();
    testWeakFundamentalIsNotReadAnOctaveUp();
    testPulledFundamentalDoesNotOutvoteTheSeries();
    testMainsHumIsNotANote();
    testTurningPegIsFollowed();
    testBodyThumpBelowTheNoteIsNotItsFundamental();
    testRingingLowStringStaysItself();

    // M8 Adaptive Frame Size & Overlap tests
    testInstrumentRecommendedFrameSize();
    testSetOverlapRatio();
    testSlidingWindowNoOverlap();
    testSlidingWindowOverlap();
    testDispatcherReconfigure();
    testAdaptiveFrameSizeBass();

    std::cout << "all tuner engine tests passed." << std::endl;

    return 0;
}