#pragma once

#include "IPitchDetector.hpp"
#include <complex>
#include <vector>

// Real-cepstrum pitch detector.
// HPF-filtered frame → Hann window → FFT → log-power spectrum → IFFT → quefrency peak.
// Particularly robust for signals with strong harmonics where the fundamental is weak.
class CepstrumPitchDetector : public IPitchDetector {
public:
    CepstrumPitchDetector(float sampleRate, int frameSize);

    DetectorResult detect(const float* frame, int n, float sampleRate) override;

    void reset() override {}
    void setFrequencyRange(float minHz, float maxHz) override;
    void setProminenceThreshold(float threshold);

private:
    float sampleRate_;
    int   frameSize_;

    float minHz_               = 60.0f;
    float maxHz_               = 1200.0f;
    float prominenceThreshold_ = 0.10f; // minimum peak prominence to be considered voiced

    std::vector<float>               hann_;
    std::vector<std::complex<float>> fftBuf_;
    std::vector<float>               logPow_;

    float peakProminence(int tauMin, int tauMax, int peakTau) const;
};
