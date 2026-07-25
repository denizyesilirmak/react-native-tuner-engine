#pragma once

#include "IPitchDetector.hpp"
#include <memory>

// Fuses two complementary pitch detectors into a single estimate:
//   - primary      (pYIN):     precise time-domain frequency, the pitch we report
//   - corroborator (cepstrum): coarser spectral estimate, independent evidence
//
// Their frequencies are never averaged — the two estimates have very different
// resolutions, so mixing them would only blur the precise one. The corroborator
// exists to confirm or contest the primary:
//   - agree → primary's frequency, confidences combined as independent evidence
//   - clash → still the primary's frequency, but its confidence dampened in
//             proportion to the corroborator's conviction
//   - solo  → the only voiced result, mildly dampened (less if it's the primary)
class DetectorFusion : public IPitchDetector {
public:
    DetectorFusion(std::unique_ptr<IPitchDetector> primary,
                   std::unique_ptr<IPitchDetector> corroborator);

    DetectorResult detect(const float* frame, int frameLength, float sampleRate) override;

    void reset() override;
    void setFrequencyRange(float minHz, float maxHz) override;

private:
    std::unique_ptr<IPitchDetector> primary_;
    std::unique_ptr<IPitchDetector> corroborator_;

    static float semitoneDistance(float frequencyA, float frequencyB);
};
