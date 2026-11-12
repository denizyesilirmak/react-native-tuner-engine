#pragma once

#include "BiquadHpf.hpp"
#include "IPitchDetector.hpp"
#include "InstrumentPresets.hpp"
#include "NoteMapper.hpp"
#include "OnsetDetector.hpp"
#include "PitchResult.hpp"
#include "PostProcessor.hpp"
#include "SnrEstimator.hpp"
#include "SpectralRefiner.hpp"
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

    // newSamples: how many samples at the end of `input` were not in the
    // previous frame (the hop). Pass it when frames overlap, so the high-pass
    // filter runs once over each sample; -1 means the whole frame is new.
    PitchResult process(const float* input, int frameCount, int newSamples = -1);

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
    // Measure the pitch from the note's partials (SpectralRefiner). On by default.
    void setSpectralRefinement(bool enabled);

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
    // Tells a fresh pluck from a note that is only ringing on: the level of
    // the last few frames, and how long since the last pluck.
    static constexpr int kPluckHistory = 6;
    float recentDb_[kPluckHistory] = {};
    int   recentCount_ = 0;
    int   recentIndex_ = 0;
    int   framesSincePluck_ = 1 << 20;
    bool  detectPluck(float rmsDb);
    PostProcessor postProcessor_;
    SpectralRefiner refiner_;
    bool refine_ = true;
    int  unrefinedFrames_ = 0; // accepted frames in a row the refiner could not measure
    // Measured fundamental over the period estimate, for the note being
    // followed; 0 = not known yet. A stiff string's period estimate sits a
    // few cents above its fundamental by a nearly constant amount.
    float fineOverCoarse_ = 0.0f;
    // The note last followed, kept after its partials could no longer be
    // measured, until the next pluck: what is left of it may not come back
    // as a new note an octave or a twelfth away.
    float fadedHz_ = 0.0f;
    // Samples since the level last jumped; -1 before the first pluck.
    int samplesSincePluck_ = -1;
    // Frames left in which a rise in level over a note in hand is still
    // being judged: a new pluck, or only the room.
    int pluckWatch_ = 0;
    // A new note's first measurement, waiting for a second to agree with it.
    float unconfirmedHz_ = 0.0f;
    // Mains hum heard in the room (its frequency), and for how many more
    // frames that is remembered.
    float humHz_ = 0.0f;
    int   humFrames_ = 0;
    // The note in hand began with no pluck near it.
    bool  bornWithoutPluck_ = false;
    bool  isMainsHum(float hz) const;
    int   unconfirmedTries_ = 0;
    // The note in hand was decided after the last pluck (not carried over it).
    bool decidedAtPluck_ = true;
    float minHz_ = 60.0f;   // the detector's range, to hold corrections to
    float maxHz_ = 1200.0f;
    NoteMapper noteMapper_;
    StringMatcher stringMatcher_;

    NoteHold noteHold_;
    float heldFrequency_ = 0.0f; // last reported frequency; 0 = no note shown
    int   missedFrames_  = 0;

    void clearHold();
    void registerMiss();

    std::vector<float> workBuffer_; // high-passed frame, kept between calls
    bool workPrimed_ = false;       // workBuffer_ holds the previous filtered frame

    void resetTracking(); // drop the note being followed: hold + post-processor

    float calculateRmsDb(const float* input, int n) const;
    float calculateRmsLinear(const float* input, int n) const;
};
