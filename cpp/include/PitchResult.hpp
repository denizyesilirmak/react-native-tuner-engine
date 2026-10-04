#pragma once

#include <cstdint>
#include <string>

// Why a frame produced (or did not produce) a pitch. Diagnostic only.
enum class PitchStage : uint8_t {
    Ok,            // pitch reported
    Gated,         // frame RMS under the noise gate
    Unvoiced,      // detector found no pitch
    LowConfidence, // voiced, but SNR-weighted confidence under the threshold
    Settling,      // PostProcessor not yet stable
};

struct PitchResult
{
    bool hasPitch = false;

    float frequency = 0.0f;
    float confidence = 0.0f;
    float rmsDb = -120.0f;

    PitchStage stage = PitchStage::Gated;
    float detectorConfidence = 0.0f; // fused detector confidence, before SNR weighting
    float snrDb = 0.0f;

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