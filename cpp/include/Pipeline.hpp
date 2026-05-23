#pragma once

#include "BiquadHpf.hpp"
#include "IPitchDetector.hpp"
#include "InstrumentPresets.hpp"
#include "NoteMapper.hpp"
#include "PitchResult.hpp"
#include "PostProcessor.hpp"
#include "SnrEstimator.hpp"
#include "StringMatcher.hpp"
#include "Window.hpp"

#include <memory>
#include <vector>

// Ordered DSP chain: HPF → Hann window → IPitchDetector → SNR weighting → PostProcessor → NoteMapper → StringMatcher.
class Pipeline {
public:
    Pipeline(int frameSize, float sampleRate, std::unique_ptr<IPitchDetector> detector);

    PitchResult process(const float* input, int frameCount);

    void setA4(float hz);
    void setNoiseGateDb(float db);
    void setConfidenceThreshold(float threshold);
    void setFrequencyRange(float minHz, float maxHz);
    void setInstrument(const std::string& name);
    void setTuning(const std::string& name);   // e.g. "guitar_standard", "" to disable
    void setPostProcessorConfig(PostProcessor::Config cfg);
    void setHpfCutoff(float hz);

private:
    int frameSize_;
    float sampleRate_;

    float noiseGateDb_           = -55.0f;
    float confidenceThreshold_   =  0.60f; // lower than raw YIN default; SNR will tighten it

    BiquadHpf hpf_;
    HannWindow window_;
    std::unique_ptr<IPitchDetector> detector_;
    SnrEstimator snr_;
    PostProcessor postProcessor_;
    NoteMapper noteMapper_;
    StringMatcher stringMatcher_;

    std::vector<float> workBuffer_; // HPF + windowing happen here (copy of input)

    float calculateRmsDb(const float* input, int n) const;
    float calculateRmsLinear(const float* input, int n) const;
};
