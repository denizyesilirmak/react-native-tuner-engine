#include "NoteMapper.hpp"
#include "TunerEngine.hpp"
#include "YinPitchDetector.hpp"

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

    std::cout << "all tuner engine tests passed." << std::endl;

    return 0;
}