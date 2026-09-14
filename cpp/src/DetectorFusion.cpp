#include "DetectorFusion.hpp"

#include <cmath>

namespace {

// Detectors within this distance count as agreeing on the same pitch.
constexpr float kAgreementSemitones = 1.0f;

// On a clash the primary's confidence is dampened in proportion to how
// credible the contesting corroborator is: a barely-voiced contester costs
// little, a fully confident one cuts the confidence to (1 - kClashPenaltyMax).
constexpr float kClashPenaltyMax = 0.4f;

// Dampening when only one detector fires. The primary alone is still fairly
// trustworthy; the corroborator alone is not precise enough to trust fully.
constexpr float kSoloPrimaryDamping      = 0.9f;
constexpr float kSoloCorroboratorDamping = 0.7f;

} // namespace

DetectorFusion::DetectorFusion(std::unique_ptr<IPitchDetector> primary,
                               std::unique_ptr<IPitchDetector> corroborator)
    : primary_(std::move(primary))
    , corroborator_(std::move(corroborator))
{}

void DetectorFusion::reset() {
    primary_->reset();
    corroborator_->reset();
}

void DetectorFusion::setFrequencyRange(float minHz, float maxHz) {
    primary_->setFrequencyRange(minHz, maxHz);
    corroborator_->setFrequencyRange(minHz, maxHz);
}

float DetectorFusion::semitoneDistance(float frequencyA, float frequencyB) {
    if (frequencyA <= 0.0f || frequencyB <= 0.0f) return 1e9f;
    return std::fabs(12.0f * std::log2(frequencyA / frequencyB));
}

DetectorResult DetectorFusion::detect(const float* frame, int frameLength, float sampleRate) {
    const DetectorResult primary      = primary_->detect(frame, frameLength, sampleRate);
    const DetectorResult corroborator = corroborator_->detect(frame, frameLength, sampleRate);

    const bool primaryVoiced      = primary.voiced && primary.confidence > 0.0f;
    const bool corroboratorVoiced = corroborator.voiced && corroborator.confidence > 0.0f;

    if (!primaryVoiced && !corroboratorVoiced) return DetectorResult{};

    if (primaryVoiced && corroboratorVoiced) {
        if (semitoneDistance(primary.frequency, corroborator.frequency) <= kAgreementSemitones) {
            // Agreement: report the primary's precise frequency. Treat the two
            // detectors as independent witnesses for the confidence:
            // P(pitch is right) = 1 - P(both witnesses are wrong).
            const float combinedConfidence =
                1.0f - (1.0f - primary.confidence) * (1.0f - corroborator.confidence);
            return DetectorResult{true, primary.frequency, combinedConfidence};
        }

        // Clash — usually an octave error in one detector. The corroborator is
        // too coarse to replace the primary's estimate, and its confidence is
        // not on the same scale as the primary's, so it never overrides — it
        // only weakens the primary in proportion to its own conviction. If the
        // primary really is wrong, the low fused confidence lets the pipeline
        // reject the frame rather than display a wrong pitch.
        const float damping = 1.0f - kClashPenaltyMax * corroborator.confidence;
        return DetectorResult{true, primary.frequency, primary.confidence * damping};
    }

    if (primaryVoiced) {
        return DetectorResult{true, primary.frequency,
                              primary.confidence * kSoloPrimaryDamping};
    }
    return DetectorResult{true, corroborator.frequency,
                          corroborator.confidence * kSoloCorroboratorDamping};
}
