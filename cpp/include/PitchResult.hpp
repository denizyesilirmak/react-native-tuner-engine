#pragma once

#include <string>

struct PitchResult
{
    bool hasPitch = false;

    float frequency = 0.0f;
    float confidence = 0.0f;
    float rmsDb = -120.0f;

    int midiNote = 0;
    std::string noteName;
    int octave = 0;

    float targetFrequency = 0.0f;
    float cents = 0.0f;

    // Set when a TuningProfile is active (via Pipeline::setTuning).
    // Empty string means no tuning is configured.
    std::string nearestString;       // e.g. "E2", "A2"
    float       stringDeviation = 0.0f; // cents from that string's target, signed
};