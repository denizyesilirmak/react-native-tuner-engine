#include "EnsembleSelector.hpp"

#include <cmath>

EnsembleSelector::EnsembleSelector(std::vector<std::unique_ptr<IPitchDetector>> detectors)
    : detectors_(std::move(detectors))
{
    resultsBuf_.resize(detectors_.size());
    voicedBuf_.reserve(detectors_.size());
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

    voicedBuf_.clear();
    for (int i = 0; i < static_cast<int>(resultsBuf_.size()); ++i) {
        const auto& r = resultsBuf_[i];
        if (r.voiced && r.confidence > 0.0f) {
            voicedBuf_.push_back({i, r.frequency, r.confidence, 0});
        }
    }

    if (voicedBuf_.empty()) return DetectorResult{};

    // Tally agreement votes between voiced entries
    for (int i = 0; i < static_cast<int>(voicedBuf_.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(voicedBuf_.size()); ++j) {
            if (withinSemitones(voicedBuf_[i].freq, voicedBuf_[j].freq)) {
                ++voicedBuf_[i].votes;
                ++voicedBuf_[j].votes;
            }
        }
    }

    // Pick winner: most votes first, then highest confidence
    const VoicedEntry* best = &voicedBuf_[0];
    for (int i = 1; i < static_cast<int>(voicedBuf_.size()); ++i) {
        const VoicedEntry& c = voicedBuf_[i];
        if (c.votes > best->votes
            || (c.votes == best->votes && c.conf > best->conf)) {
            best = &c;
        }
    }

    // Average the frequency (and confidence) of all detectors that agree with winner
    float freqSum  = best->freq;
    float confSum  = best->conf;
    int   agreeing = 1;

    for (int i = 0; i < static_cast<int>(voicedBuf_.size()); ++i) {
        if (&voicedBuf_[i] != best && withinSemitones(voicedBuf_[i].freq, best->freq)) {
            freqSum += voicedBuf_[i].freq;
            confSum += voicedBuf_[i].conf;
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
