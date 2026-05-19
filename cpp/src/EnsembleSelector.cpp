#include "EnsembleSelector.hpp"

#include <cmath>

EnsembleSelector::EnsembleSelector(std::vector<std::unique_ptr<IPitchDetector>> detectors)
    : detectors_(std::move(detectors))
{
    resultsBuf_.resize(detectors_.size());
}

void EnsembleSelector::reset() {
    for (auto& d : detectors_) d->reset();
}

void EnsembleSelector::setFrequencyRange(float minHz, float maxHz) {
    for (auto& d : detectors_) d->setFrequencyRange(minHz, maxHz);
}

void EnsembleSelector::setThreshold(float threshold) {
    for (auto& d : detectors_) d->setThreshold(threshold);
}

bool EnsembleSelector::withinSemitones(float f1, float f2, float tolerance) {
    if (f1 <= 0.0f || f2 <= 0.0f) return false;
    return std::fabs(12.0f * std::log2(f1 / f2)) <= tolerance;
}

DetectorResult EnsembleSelector::detect(const float* frame, int n, float sampleRate) {
    // Run all detectors into the pre-allocated buffer
    for (int i = 0; i < static_cast<int>(detectors_.size()); ++i) {
        resultsBuf_[i] = detectors_[i]->detect(frame, n, sampleRate);
    }

    // Work with a small stack-local view of voiced results to avoid heap allocation.
    // Maximum 8 detectors is more than enough for any realistic ensemble.
    struct Entry { int idx; float freq; float conf; int votes; };
    Entry voiced[8];
    int voicedCount = 0;

    for (int i = 0; i < static_cast<int>(resultsBuf_.size()) && voicedCount < 8; ++i) {
        const auto& r = resultsBuf_[i];
        if (r.voiced && r.confidence > 0.0f) {
            voiced[voicedCount++] = {i, r.frequency, r.confidence, 0};
        }
    }

    if (voicedCount == 0) return DetectorResult{};

    // Tally agreement votes between voiced entries
    for (int i = 0; i < voicedCount; ++i) {
        for (int j = i + 1; j < voicedCount; ++j) {
            if (withinSemitones(voiced[i].freq, voiced[j].freq)) {
                ++voiced[i].votes;
                ++voiced[j].votes;
            }
        }
    }

    // Pick winner: most votes first, then highest confidence
    const Entry* best = &voiced[0];
    for (int i = 1; i < voicedCount; ++i) {
        const Entry& c = voiced[i];
        if (c.votes > best->votes
            || (c.votes == best->votes && c.conf > best->conf)) {
            best = &c;
        }
    }

    // Average the frequency (and confidence) of all detectors that agree with winner
    float freqSum  = best->freq;
    float confSum  = best->conf;
    int   agreeing = 1;

    for (int i = 0; i < voicedCount; ++i) {
        if (&voiced[i] != best && withinSemitones(voiced[i].freq, best->freq)) {
            freqSum += voiced[i].freq;
            confSum += voiced[i].conf;
            ++agreeing;
        }
    }

    const float avgFreq = freqSum / static_cast<float>(agreeing);
    float avgConf = confSum  / static_cast<float>(agreeing);

    // Confidence bonus for agreement, penalty for a lone detector
    avgConf = (agreeing > 1)
              ? std::min(1.0f, avgConf * 1.1f)
              : avgConf * 0.85f;

    return DetectorResult{true, avgFreq, avgConf};
}
