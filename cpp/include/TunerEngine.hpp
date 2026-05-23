#pragma once

#include "Pipeline.hpp"
#include "PostProcessor.hpp"
#include "PitchResult.hpp"

#include <memory>
#include <string>

// Facade that owns a Pipeline. The external API is identical to the pre-M2 TunerEngine
// so AudioFrameDispatcher and all existing tests require no changes.
class TunerEngine {
public:
    TunerEngine(float sampleRate, int frameSize);

    PitchResult process(const float* input, int frameCount);

    void setA4(float a4);
    void setNoiseGateDb(float db);
    void setConfidenceThreshold(float value);
    void setFrequencyRange(float minFrequency, float maxFrequency);
    void setInstrument(const std::string& name);
    void setTuning(const std::string& name);
    void setPostProcessorConfig(PostProcessor::Config cfg);
    void setHpfCutoff(float hz);

private:
    std::unique_ptr<Pipeline> pipeline_;
};
