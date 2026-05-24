#include "NoteMapper.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

NoteMapper::NoteMapper(float a4) : a4_(a4) {}

void NoteMapper::setA4(float value) {
    if (value <= 0.0f) {
        throw std::invalid_argument("A4 must be positive");
    }

    a4_ = value;
}

void NoteMapper::setTemperament(const std::string& name) {
    useJust_ = (name == "just");
}

PitchResult NoteMapper::map(float frequency, float confidence, float rmsDb) const {
    PitchResult result;

    if (frequency <= 0.0f) {
        return result;
    }

    static const std::array<const char*, 12> names = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    const int midi = static_cast<int>(
        std::round(69.0f + 12.0f * std::log2(frequency / a4_))
    );

    // Equal-temperament target
    float target = a4_ * std::pow(2.0f, (midi - 69) / 12.0f);

    // Apply just-intonation offset if enabled
    if (useJust_) {
        const int pitchClass = ((midi % 12) + 12) % 12; // 0=C .. 11=B
        target *= std::pow(2.0f, kJustCentsOffset[pitchClass] / 1200.0f);
    }

    const float cents = 1200.0f * std::log2(frequency / target);

    result.hasPitch = true;
    result.frequency = frequency;
    result.confidence = confidence;
    result.rmsDb = rmsDb;
    result.midiNote = midi;
    result.noteName = names[((midi % 12) + 12) % 12];
    result.octave = midi / 12 - 1;
    result.targetFrequency = target;
    result.cents = cents;

    return result;
}