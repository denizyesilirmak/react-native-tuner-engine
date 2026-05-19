#include "NoteMapper.hpp"
#include "TunerEngine.hpp"
#include "YinPitchDetector.hpp"
#include "PyinPitchDetector.hpp"
#include "CepstrumPitchDetector.hpp"
#include "EnsembleSelector.hpp"
#include "BiquadHpf.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
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

static void testYin(float inputFrequency) {
    constexpr float sampleRate = 48000.0f;
    constexpr int frameSize = 4096;

    auto buffer = generateSine(inputFrequency, sampleRate, frameSize);

    YinPitchDetector detector(sampleRate, frameSize);
    auto result = detector.detect(buffer.data(), static_cast<int>(buffer.size()));

    std::cout
        << "yin input: " << inputFrequency
        << " detected: " << result.frequency
        << " confidence: " << result.confidence
        << std::endl;

    assert(result.hasPitch);
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

// --- M3: per-detector and ensemble tests ---

static void testDetectorOnSine(float freq, const std::string& label) {
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;
    auto buf = generateSine(freq, sr, n);

    BiquadHpf hpf(sr, 70.0f);
    hpf.process(buf.data(), n);

    YinPitchDetector  yin(sr, n);
    PyinPitchDetector pyin(sr, n);

    auto ry  = yin.detect(buf.data(), n, sr);
    auto rpy = pyin.detect(buf.data(), n, sr);

    std::cout << "detector " << label << " @ " << freq << " Hz:\n"
              << "  YIN  freq=" << ry.frequency  << " conf=" << ry.confidence  << "\n"
              << "  PYIN freq=" << rpy.frequency << " conf=" << rpy.confidence << "\n";

    assert(ry.voiced);
    assertNear(ry.frequency, freq, freq * 0.01f);  // within 1%

    assert(rpy.voiced);
    assertNear(rpy.frequency, freq, freq * 0.01f); // PYIN must agree with YIN
}

static void testEnsembleAgreementBonus() {
    // When YIN and PYIN agree, the ensemble should produce confidence > either detector alone.
    constexpr float sr  = 48000.0f;
    constexpr int   n   = 4096;
    constexpr float f0  = 196.0f; // G3

    auto buf = generateSine(f0, sr, n * 6); // 6 continuous frames for HPF warmup

    std::vector<std::unique_ptr<IPitchDetector>> detectors;
    detectors.push_back(std::make_unique<YinPitchDetector>(sr, n));
    detectors.push_back(std::make_unique<PyinPitchDetector>(sr, n));
    detectors.push_back(std::make_unique<CepstrumPitchDetector>(sr, n));
    EnsembleSelector ensemble(std::move(detectors));

    BiquadHpf hpf(sr, 70.0f);

    DetectorResult result;
    for (int f = 0; f < 6; ++f) {
        std::vector<float> frame(buf.begin() + f * n, buf.begin() + (f + 1) * n);
        hpf.process(frame.data(), n);
        result = ensemble.detect(frame.data(), n, sr);
    }

    std::cout << "ensemble G3: voiced=" << result.voiced
              << " freq=" << result.frequency
              << " conf=" << result.confidence << "\n";

    assert(result.voiced);
    assertNear(result.frequency, f0, f0 * 0.01f);
    assert(result.confidence > 0.85f); // agreement bonus applied
}

static void testEnsembleOctaveSafety() {
    // D3 (146.83 Hz): tau0 and 2*tau0 both fit in the search range.
    // The ensemble must return the fundamental, not the sub-octave.
    constexpr float sr = 48000.0f;
    constexpr int   n  = 4096;

    TunerEngine engine(sr, n);
    auto buf = generateSine(146.83f, sr, n * 6);

    PitchResult result;
    for (int f = 0; f < 6; ++f) {
        result = engine.process(buf.data() + f * n, n);
    }

    std::cout << "ensemble octave safety D3: "
              << result.noteName << result.octave
              << " freq=" << result.frequency << "\n";

    assert(result.hasPitch);
    assert(result.noteName == "D");
    assert(result.octave == 3);
}

int main() {
    testNoteMapper();

    testYin(82.41f);
    testYin(110.0f);
    testYin(146.83f);
    testYin(196.0f);
    testYin(246.94f);
    testYin(329.63f);

    testTunerEngine(82.41f, "E", 2);
    testTunerEngine(110.0f, "A", 2);
    testTunerEngine(146.83f, "D", 3);
    testTunerEngine(196.0f, "G", 3);
    testTunerEngine(246.94f, "B", 3);
    testTunerEngine(329.63f, "E", 4);

    testTunerEngineSilence();
    testTunerEngineQuietSignal();

    // M3 per-detector and ensemble tests
    testDetectorOnSine(82.41f,  "E2");
    testDetectorOnSine(110.0f,  "A2");
    testDetectorOnSine(146.83f, "D3");
    testDetectorOnSine(196.0f,  "G3");
    testDetectorOnSine(329.63f, "E4");
    testDetectorOnSine(440.0f,  "A4");
    testEnsembleAgreementBonus();
    testEnsembleOctaveSafety();

    // M2 DSP hardening tests
    testPipelineCleanSine(82.41f,  "E", 2);
    testPipelineCleanSine(110.0f,  "A", 2);
    testPipelineCleanSine(196.0f,  "G", 3);
    testPipelineCleanSine(329.63f, "E", 4);
    testPipelineCleanSine(440.0f,  "A", 4);
    testPipelineNoisySignal();
    testPipelineHysteresis();
    testInstrumentPreset();

    std::cout << "all tuner engine tests passed." << std::endl;

    return 0;
}