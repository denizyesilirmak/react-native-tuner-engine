#pragma once

#include "BiquadHpf.hpp"
#include "IPitchDetector.hpp"
#include "InstrumentPresets.hpp"
#include "NoteMapper.hpp"
#include "OnsetDetector.hpp"
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
    // Continues a note that is already shown through frames whose confidence
    // dips below the normal threshold (a decaying string). It never starts a note.
    struct NoteHold {
        float minConfidence   = 0.4f;  // weighted confidence needed to continue the note
        float maxCents        = 25.0f; // max distance from the last reported frequency
        int   maxMissedFrames = 2;     // rejected frames tolerated before the hold is dropped
        bool  enabled         = true;
    };

    Pipeline(int frameSize, float sampleRate, std::unique_ptr<IPitchDetector> detector);

    PitchResult process(const float* input, int frameCount);

    void setA4(float hz);
    void setNoiseGateDb(float db);
    void setConfidenceThreshold(float threshold);
    void setFrequencyRange(float minHz, float maxHz);
    void setInstrument(const std::string& name);
    void setTuning(const std::string& name);   // e.g. "guitar_standard", "" to disable
    void setTemperament(const std::string& name); // "equal" or "just"
    void setPostProcessorConfig(PostProcessor::Config cfg);
    void setHpfCutoff(float hz);
    void setOnsetDetectionEnabled(bool enabled);
    void setOnsetConfig(OnsetDetector::Config cfg);
    void setNoteHold(NoteHold hold);

private:
    int frameSize_;
    float sampleRate_;

    float noiseGateDb_           = -70.0f;
    float confidenceThreshold_   =  0.75f;

    BiquadHpf hpf_;
    HannWindow window_;
    std::unique_ptr<IPitchDetector> detector_;
    SnrEstimator snr_;
    OnsetDetector onsetDetector_;
    PostProcessor postProcessor_;
    NoteMapper noteMapper_;
    StringMatcher stringMatcher_;

    NoteHold noteHold_;
    float heldFrequency_ = 0.0f; // last reported frequency; 0 = no note shown
    int   missedFrames_  = 0;

    void clearHold();
    void registerMiss();

    std::vector<float> workBuffer_; // HPF + windowing happen here (copy of input)

    float calculateRmsDb(const float* input, int n) const;
    float calculateRmsLinear(const float* input, int n) const;
};
