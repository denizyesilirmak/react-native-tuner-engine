#include "NoteMapper.hpp"
#include "OnsetDetector.hpp"
#include "TunerEngine.hpp"
#include "PyinPitchDetector.hpp"
#include "CepstrumPitchDetector.hpp"
#include "DetectorFusion.hpp"
#include "BiquadHpf.hpp"
#include "AudioFrameDispatcher.hpp"
#include "InstrumentPresets.hpp"

#include <cassert>
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