#include "Pipeline.hpp"

#include <algorithm>
#include <cmath>

static constexpr float kMinLinear = 1e-7f;
static constexpr float kRefinedEmaAlpha = 0.7f;
// ...and for a followed note that has become faint: slow whatever the step.
static constexpr float kFaintEmaAlpha = 0.06f;
// A step this large in a followed note needs a second frame to agree.
static constexpr float kStepCents = 4.0f;
// Mains hum: how close to 50/60/100/120 Hz, how long after a pluck a note is
// not suspected, and how long hum once heard is remembered.
static constexpr float kHumCents = 10.0f;
static constexpr float kHumNoPluckSeconds = 1.0f;
static constexpr float kHumMemorySeconds = 30.0f;
// A new note's first two measurements must agree this closely, for at most
// this many attempts.
static constexpr float kConfirmCents = 3.0f;
static constexpr int   kConfirmMaxTries = 3;
// ...and only this soon after a pluck; a note met in mid-ring is steady.
static constexpr float kConfirmSeconds = 0.5f;
// The furthest one faint frame's measurement may sit from the reading.
static constexpr float kFaintMaxStepCents = 8.0f;
static constexpr float kRefineGraceSeconds = 0.25f;
// A subharmonic correction is only believed when the corrected note's
// partials make up most of the sound.
static constexpr float kMinExplained = 0.6f;
// How far past the edge of the instrument's range a measured note may sit
// (about 50 cents).
static constexpr float kRangeSlack = 0.97f;
// A rise in level this large over the recent envelope is a fresh pluck.
static constexpr float kPluckRiseDb = 6.0f;
// A rise in level over a note in hand is judged for this many frames.
static constexpr int kPluckWatchFrames = 4;
// How long after a pluck the octave of the note in hand may still be
// corrected downwards.
static constexpr float kSettleSeconds = 0.3f;
// Frames after a pluck in which another is not looked for (the attack itself
// keeps rising for a frame or two).
static constexpr int kPluckCooldownFrames = 3;
// How far a followed note may move between two frames (a peg being turned).
static constexpr float kFollowCents = 50.0f;
// A followed note's partials must still account for this share of the window;
// under it the reading is noise with the note's name on it.
static constexpr float kMinFollowExplained = 0.4f;

// True when `hz` is a simple multiple or fraction of `base` (an octave, a
// twelfth, two octaves... within about 50 cents). The same note is not.
static bool isHarmonicOf(float hz, float base) {
    const float ratio = hz > base ? hz / base : base / hz;
    const float nearest = std::round(ratio);
    return nearest >= 2.0f && nearest <= 6.0f && std::fabs(ratio - nearest) <= 0.03f * nearest;
}

Pipeline::Pipeline(int frameSize, float sampleRate, std::unique_ptr<IPitchDetector> detector)
    : frameSize_(frameSize)
    , sampleRate_(sampleRate)
    , hpf_(sampleRate, 70.0f)       // 70 Hz HPF — removes DC and sub-bass rumble
    , window_(frameSize)
    , detector_(std::move(detector))
    , refiner_(sampleRate)
    , workBuffer_(static_cast<size_t>(frameSize))
{}

// A pluck: the level stands kPluckRiseDb over the quietest of the last few
// frames. Compared with real past levels, not an envelope that has to climb
// from nothing: that one "heard" a pluck every few frames for the first
// second after the microphone opened, and its cooldown then hid a real one.
bool Pipeline::detectPluck(float rmsDb) {
    bool plucked = false;
    if (recentCount_ >= 3 && framesSincePluck_ >= kPluckCooldownFrames) {
        float quietest = recentDb_[0];
        for (int i = 1; i < recentCount_; ++i) quietest = std::min(quietest, recentDb_[i]);
        plucked = rmsDb - quietest >= kPluckRiseDb;
    }
    if (plucked) {
        // The quiet frames before this pluck must not be what the next few
        // frames are compared with, or the same pluck is found again as soon
        // as the cooldown ends.
        for (int i = 0; i < kPluckHistory; ++i) recentDb_[i] = rmsDb;
        recentCount_ = kPluckHistory;
    } else {
        recentDb_[recentIndex_] = rmsDb;
        recentIndex_ = (recentIndex_ + 1) % kPluckHistory;
        if (recentCount_ < kPluckHistory) ++recentCount_;
    }
    framesSincePluck_ = plucked ? 0 : std::min(framesSincePluck_ + 1, 1 << 20);
    return plucked;
}

PitchResult Pipeline::process(const float* input, int frameCount, int newSamples) {
    if (!input || frameCount < frameSize_) return PitchResult{};

    // --- High-pass, before the gate so the filter sees every sample once.
    // Overlapping frames only filter their new tail: running the stateful
    // filter over the overlap again biased low strings by up to 2.5 cents.
    if (workPrimed_ && newSamples > 0 && newSamples < frameSize_) {
        const int retain = frameSize_ - newSamples;
        std::copy(workBuffer_.begin() + newSamples, workBuffer_.end(), workBuffer_.begin());
        std::copy(input + retain, input + frameSize_, workBuffer_.begin() + retain);
        hpf_.process(workBuffer_.data() + retain, newSamples);
        refiner_.push(workBuffer_.data() + retain, newSamples);
    } else {
        std::copy(input, input + frameSize_, workBuffer_.begin());
        hpf_.process(workBuffer_.data(), frameSize_);
        workPrimed_ = true;
        refiner_.push(workBuffer_.data(), frameSize_);
    }

    // --- RMS gate ---
    const float rmsLinear = calculateRmsLinear(input, frameSize_);
    const float rmsDb     = 20.0f * std::log10(std::max(rmsLinear, kMinLinear));
    // Every frame, silent ones too: the pluck is a rise over what came before.
    const bool plucked = detectPluck(rmsDb);

    if (rmsDb < noiseGateDb_) {
        // Gated frames are the best view of the room's noise floor.
        snr_.update(rmsLinear, false);
        // Silence ends the note: the next one must not start from the median
        // and EMA of the last, or it reads as the old string for a few frames.
        resetTracking();
        PitchResult silent;
        silent.rmsDb = rmsDb;
        silent.stage = PitchStage::Gated;
        return silent;
    }

    // --- Onset detection (no-op when disabled — single branch) ---
    if (onsetDetector_.detect(rmsDb)) {
        postProcessor_.reset();
        clearHold();
    }
    // A fresh pluck ends whatever was being followed: the detector decides
    // anew which string it is.
    if (plucked) {
        // Whatever was waiting to be confirmed belonged to the sound before.
        unconfirmedHz_ = 0.0f;
        unconfirmedTries_ = 0;
        if (refine_ && heldFrequency_ > 0.0f) {
            // Over a note in hand a rise in level is not yet a pluck. A note
            // that has sunk to the room's level rides its noise, which
            // swings by 6 dB and more from frame to frame; each swing used
            // to end the note. The note stays until a new one shows itself.
            pluckWatch_ = kPluckWatchFrames;
            decidedAtPluck_ = false;
        } else {
            resetTracking();
            fadedHz_ = 0.0f;
            decidedAtPluck_ = true;
        }
        // The pluck fell somewhere in this frame; count from its start.
        samplesSincePluck_ = frameSize_;
    } else if (samplesSincePluck_ >= 0 && samplesSincePluck_ < (1 << 28)) {
        samplesSincePluck_ += frameSize_;
    }

    if (humFrames_ > 0) --humFrames_;

    // --- Pitch detection ---
    DetectorResult det = detector_->detect(workBuffer_.data(), frameSize_, sampleRate_);

    // --- SNR-weighted confidence ---
    const float snrDb     = snr_.update(rmsLinear, det.voiced);
    const float snrWeight = SnrEstimator::snrToWeight(snrDb);
    float weightedConf = det.confidence * snrWeight;

    // --- The note already being followed comes first ---
    // A ringing string keeps its pitch; what changes is which of its partials
    // is loudest. Asked again every frame, the period detector follows that:
    // a low E whose second partial outlasts its fundamental became an E3
    // after four seconds, a B3 fading into the room's rumble became a B1.
    // So until a fresh pluck, the note in hand is measured where it is, and
    // the detector is only consulted once its partials are gone.
    float measuredHz = det.frequency;
    bool refinedReading = false;
    bool followed = false;
    bool faint = false;
    // The first decision after a pluck rests on a few periods, too short a
    // stretch to show a weak fundamental: a string whose even partials carry
    // nearly all of its energy is taken for the octave above, and following
    // would then keep it there. So for a moment after the pluck the note is
    // asked for once more, and an answer an octave under the one in hand
    // replaces it.
    const bool watching = pluckWatch_ > 0 && heldFrequency_ > 0.0f;
    if (pluckWatch_ > 0 && heldFrequency_ > 0.0f) {
        --pluckWatch_;
        if (refine_ && det.voiced && det.frequency > 0.0f) {
            const float fresh = refiner_.refine(det.frequency, samplesSincePluck_);
            if (fresh >= minHz_ * kRangeSlack && fresh <= maxHz_ / kRangeSlack
                && refiner_.lastPartialCount() >= 2
                && refiner_.lastExplained() >= kMinExplained) {
                // A clean series that is most of the sound: a string was
                // plucked, the same one or another. Start over from it.
                resetTracking();
                fadedHz_ = 0.0f;
                pluckWatch_ = 0;
                decidedAtPluck_ = true;
            }
        }
    } else if (heldFrequency_ <= 0.0f) {
        pluckWatch_ = 0;
    }
    if (decidedAtPluck_ && refine_ && heldFrequency_ > 0.0f && det.voiced && det.frequency > 0.0f
        && samplesSincePluck_ >= 0
        && samplesSincePluck_ <= static_cast<int>(kSettleSeconds * sampleRate_)) {
        const float again = refiner_.refine(det.frequency, samplesSincePluck_);
        if (again > 0.0f && again >= minHz_ * kRangeSlack
            && refiner_.lastPartialCount() >= 2
            && refiner_.lastExplained() >= kMinExplained
            && std::fabs(1200.0f * std::log2(again / (0.5f * heldFrequency_))) <= kFollowCents) {
            postProcessor_.reset();
            heldFrequency_ = again;
        }
    }
    if (refine_ && heldFrequency_ > 0.0f) {
        const float kept = refiner_.refineKnown(heldFrequency_);
        if (kept > 0.0f
            && std::fabs(1200.0f * std::log2(kept / heldFrequency_)) <= kFollowCents) {
            if (watching && heldFrequency_ > 0.0f
                && std::fabs(1200.0f * std::log2(kept / heldFrequency_)) > kConfirmCents) {
                // The same string plucked again: for a few frames the window
                // holds the attack, and a B3 plucked every 0.3 s read 10 to
                // 15 cents flat at each stroke. Wait for it to settle.
                PitchResult nopit;
                nopit.rmsDb = rmsDb;
                nopit.stage = PitchStage::Settling;
                nopit.detectorConfidence = det.confidence;
                nopit.snrDb = snrDb;
                return nopit;
            }
            measuredHz = kept;
            refinedReading = true;
            followed = true;
            // Sinking into the room: its partials still stand out, but they
            // are a small part of what the microphone hears and each frame's
            // measurement wanders by several cents. Keep the reading and let
            // it move only slowly, instead of dropping the note a second
            // into a three-second ring.
            // (Not in the moments after a pluck: there the partials are a
            // small part of the window because the attack fills it.)
            faint = refiner_.lastExplained() < kMinFollowExplained
                && (samplesSincePluck_ < 0
                    || samplesSincePluck_ > static_cast<int>(kConfirmSeconds * sampleRate_));
            weightedConf = std::max(weightedConf, confidenceThreshold_);
        } else if (pluckWatch_ > 0) {
            // Gone at a rise in level: the pluck was real and what it
            // brought is not readable yet. Nothing of the old note is owed.
            resetTracking();
            fadedHz_ = 0.0f;
            pluckWatch_ = 0;
            decidedAtPluck_ = true;
        } else {
            fadedHz_ = heldFrequency_;
        }
    }

    // --- Otherwise: measure the note the detector names ---
    // Its partials say exactly where it is, and whether the detector
    // answered with a subharmonic of it.
    if (!followed && refine_ && det.voiced && det.frequency > 0.0f) {
        const float refined = refiner_.refine(det.frequency, samplesSincePluck_);
        if (refined > 0.0f && (refined < minHz_ * kRangeSlack || refined > maxHz_ / kRangeSlack)) {
            // The partials led outside the instrument's range: whatever
            // that is (hum at the low edge, a harmonic above the top), it is
            // not one of its notes.
            PitchResult nopit;
            nopit.rmsDb = rmsDb;
            nopit.stage = PitchStage::Unvoiced;
            nopit.detectorConfidence = det.confidence;
            nopit.snrDb = snrDb;
            registerMiss();
            return nopit;
        }
        if (refined > 0.0f && fadedHz_ > 0.0f && isHarmonicOf(refined, fadedHz_)) {
            // What is left of the note that just faded (its second partial
            // outlives the rest), not a new note: nobody has plucked.
            PitchResult nopit;
            nopit.rmsDb = rmsDb;
            nopit.stage = PitchStage::Unvoiced;
            nopit.detectorConfidence = det.confidence;
            nopit.snrDb = snrDb;
            registerMiss();
            return nopit;
        }
        if (refined > 0.0f && refiner_.lastMultiple() > 1 && !refiner_.lastMultipleByArrival()
            && samplesSincePluck_ >= 0
            && samplesSincePluck_ <= static_cast<int>(kSettleSeconds * sampleRate_)) {
            // Raised an octave or more over the detector's word, on the few
            // periods there are just after a pluck: a banjo's B showed as
            // the B above for two frames, until its odd partials could be
            // made out. Not shown until the note has settled.
            PitchResult nopit;
            nopit.rmsDb = rmsDb;
            nopit.stage = PitchStage::Settling;
            nopit.detectorConfidence = det.confidence;
            nopit.snrDb = snrDb;
            return nopit;
        }
        if (refined > 0.0f) {
            measuredHz = refined;
            refinedReading = true;
            // Found again by the detector with no pluck in between: the same
            // string, too faint to follow a moment ago. Still faint.
            if (heldFrequency_ > 0.0f && pluckWatch_ == 0
                && samplesSincePluck_ > static_cast<int>(kSettleSeconds * sampleRate_)
                && std::fabs(1200.0f * std::log2(refined / heldFrequency_)) <= kFollowCents) {
                faint = true;
            }
            if (refiner_.lastPartialCount() >= 2 && refiner_.lastExplained() >= kMinExplained) {
                // The detector proposes, the spectrum confirms. A low string
                // whose second partial dwarfs its fundamental leaves the
                // period detector torn between two octaves and unsure of
                // either (confidence 0.2 to 0.5), while its partials form a
                // clean series that is nearly all of the sound. That series
                // is the evidence; the detector's doubt is not.
                weightedConf = std::max(
                    weightedConf, static_cast<float>(refiner_.lastExplained()) * snrWeight);
            }
            if (refiner_.lastMultiple() > 1) {
                if (refined > maxHz_ || refiner_.lastExplained() < kMinExplained) {
                    // A correction to a note outside the instrument's range,
                    // or to partials that are a small part of what is in the
                    // room (hum, noise): not a note. Keep the detector's word.
                    measuredHz = det.frequency;
                    refinedReading = false;
                } else {
                    // The period detector was unsure because it was
                    // describing two things at once. What counts now is how
                    // clearly the note that was measured stands out.
                    weightedConf = std::max(
                        weightedConf,
                        static_cast<float>(refiner_.lastDominance() * refiner_.lastExplained()) * snrWeight);
                }
            }
        }
    }

    // A decaying string's confidence sinks below the threshold while the note
    // is still clearly there. A frame close to the note already shown may
    // continue it at a lower confidence; it can never start a note.
    bool held = false;
    if (noteHold_.enabled && heldFrequency_ > 0.0f && det.voiced
        && weightedConf < confidenceThreshold_ && weightedConf >= noteHold_.minConfidence
        && measuredHz > 0.0f) {
        const float cents = 1200.0f * std::log2(measuredHz / heldFrequency_);
        held = std::fabs(cents) <= noteHold_.maxCents;
    }

    if (!followed && !held && (!det.voiced || weightedConf < confidenceThreshold_)) {
        registerMiss();
        PitchResult nopit;
        nopit.rmsDb = rmsDb;
        nopit.stage = det.voiced ? PitchStage::LowConfidence : PitchStage::Unvoiced;
        nopit.detectorConfidence = det.confidence;
        nopit.snrDb = snrDb;
        return nopit;
    }

    if (refine_) {
        if (refinedReading) {
            unrefinedFrames_ = 0;
            if (!followed && refiner_.lastMultiple() == 1 && refiner_.lastDivisor() == 1) {
                const float ratio = measuredHz / det.frequency;
                fineOverCoarse_ = fineOverCoarse_ > 0.0f
                    ? 0.7f * fineOverCoarse_ + 0.3f * ratio
                    : ratio;
            }
        } else if (fineOverCoarse_ > 0.0f && heldFrequency_ > 0.0f
                   && std::fabs(1200.0f * std::log2(det.frequency * fineOverCoarse_ / heldFrequency_)) <= 50.0f) {
            // The note already being followed, in a frame its partials could
            // not be measured in (a sudden change of level). Carry on from
            // the period estimate, corrected by what this string has shown.
            measuredHz = det.frequency * fineOverCoarse_;
        } else {
            // A young note cannot be measured yet, and the period estimate
            // of its first frames is pulled sharp by the bright attack. Say
            // nothing for a moment rather than show that; a sound the
            // refiner never locks onto falls back to the period estimate.
            const int grace = static_cast<int>(
                std::ceil(kRefineGraceSeconds * sampleRate_ / static_cast<float>(frameSize_)));
            if (refiner_.hasHistoryFor(det.frequency) && ++unrefinedFrames_ <= grace) {
                PitchResult nopit;
                nopit.rmsDb = rmsDb;
                nopit.stage = PitchStage::Settling;
                nopit.detectorConfidence = det.confidence;
                nopit.snrDb = snrDb;
                return nopit;
            }
        }
    }

    // A followed note does not leap. Turning a peg moves it frame after
    // frame in one direction; a single frame 5 or 10 cents away, back the
    // next, is one partial caught in the room's noise at the end of a ring.
    // A step waits one frame for the next to go the same way.
    if (followed && !faint && heldFrequency_ > 0.0f) {
        const float cents = 1200.0f * std::log2(measuredHz / heldFrequency_);
        const int direction = cents > 0.0f ? 1 : -1;
        if (std::fabs(cents) < kStepCents) {
            pendingStep_ = 0;
        } else if (pendingStep_ == direction) {
            pendingStep_ = 0;               // borne out: take it
        } else {
            pendingStep_ = direction;
            measuredHz = heldFrequency_;    // not yet
        }
    } else {
        pendingStep_ = 0;
    }

    // A faint note is where it was: a dying string does not change pitch,
    // only its measurement gets worse (27 cents off in single frames at the
    // room's level). Such a frame may nudge the reading, not move it.
    if (faint && heldFrequency_ > 0.0f) {
        const float cents = 1200.0f * std::log2(measuredHz / heldFrequency_);
        if (std::fabs(cents) > kFaintMaxStepCents) {
            measuredHz = heldFrequency_
                * std::pow(2.0f, std::copysign(kFaintMaxStepCents, cents) / 1200.0f);
        }
    }

    if (measuredHz > 0.0f && (heldFrequency_ <= 0.0f || bornWithoutPluck_) && isMainsHum(measuredHz)) {
        // 50 or 60 Hz, or twice that, to within a few cents, and no pluck
        // brought it: the mains, through an amplifier or a lamp. It has a
        // clean series and never decays, so nothing else tells it from a
        // bass string. Remembered, so that a pluck does not make it one.
        // (A note that began with no pluck may have been taken up a few
        // cents off, before it could be measured; it is judged again here.)
        const bool noPluck = heldFrequency_ > 0.0f || samplesSincePluck_ < 0
            || samplesSincePluck_ > static_cast<int>(kHumNoPluckSeconds * sampleRate_);
        if (noPluck) {
            humHz_ = measuredHz;
            humFrames_ = static_cast<int>(kHumMemorySeconds * sampleRate_ / frameSize_);
        }
        if (noPluck || humFrames_ > 0) {
            PitchResult nopit;
            nopit.rmsDb = rmsDb;
            nopit.stage = PitchStage::Unvoiced;
            nopit.detectorConfidence = det.confidence;
            nopit.snrDb = snrDb;
            if (heldFrequency_ > 0.0f) resetTracking(); else registerMiss();
            fadedHz_ = 0.0f;
            return nopit;
        }
    }
    // A new note's first measurement is taken while the string is still
    // settling from the pluck and can sit 5 to 20 cents from the second one.
    // It is shown once a second measurement agrees with it.
    if (refinedReading && heldFrequency_ <= 0.0f && samplesSincePluck_ >= 0
        && samplesSincePluck_ <= static_cast<int>(kConfirmSeconds * sampleRate_)) {
        const bool agrees = unconfirmedHz_ > 0.0f
            && std::fabs(1200.0f * std::log2(measuredHz / unconfirmedHz_)) <= kConfirmCents;
        if (!agrees && unconfirmedTries_ < kConfirmMaxTries) {
            unconfirmedHz_ = measuredHz;
            ++unconfirmedTries_;
            PitchResult nopit;
            nopit.rmsDb = rmsDb;
            nopit.stage = PitchStage::Settling;
            nopit.detectorConfidence = det.confidence;
            nopit.snrDb = snrDb;
            return nopit;
        }
    }
    unconfirmedHz_ = 0.0f;
    unconfirmedTries_ = 0;

    // --- Post-process: median + EMA + hysteresis ---
    // A refined reading is already steady frame to frame; heavy smoothing
    // would only make it lag the string.
    PostProcessor::Result pp = postProcessor_.process(
        measuredHz, weightedConf,
        refinedReading ? (faint ? kFaintEmaAlpha : kRefinedEmaAlpha) : -1.0f, refinedReading);

    if (!pp.isStable || pp.frequency <= 0.0f) {
        registerMiss();
        PitchResult nopit;
        nopit.rmsDb = rmsDb;
        nopit.stage = PitchStage::Settling;
        nopit.detectorConfidence = det.confidence;
        nopit.snrDb = snrDb;
        return nopit;
    }

    // --- Note mapping ---
    // The mapper's cents follow A4 and the temperament; the post-processor's
    // are equal-temperament only, and its frequency now stays inside one note.
    PitchResult result = noteMapper_.map(pp.frequency, weightedConf, rmsDb);
    if (heldFrequency_ <= 0.0f) {
        bornWithoutPluck_ = samplesSincePluck_ < 0
            || samplesSincePluck_ > static_cast<int>(kHumNoPluckSeconds * sampleRate_);
    }
    heldFrequency_ = pp.frequency;
    missedFrames_  = 0;
    result.stage = PitchStage::Ok;
    result.detectorConfidence = det.confidence;
    result.snrDb = snrDb;

    // --- String matching (optional, only when a TuningProfile is active) ---
    if (stringMatcher_.hasTuning()) {
        auto m = stringMatcher_.match(pp.frequency);
        if (m) {
            result.nearestString    = m->name;
            result.stringDeviation  = m->deviationCents;
        }
    }

    return result;
}

void Pipeline::setA4(float hz) {
    noteMapper_.setA4(hz);
    postProcessor_.setA4(hz);
    clearHold();
}

void Pipeline::setNoiseGateDb(float db) {
    noiseGateDb_ = db;
}

void Pipeline::setConfidenceThreshold(float threshold) {
    confidenceThreshold_ = threshold;
}

void Pipeline::setFrequencyRange(float minHz, float maxHz) {
    minHz_ = minHz;
    maxHz_ = maxHz;
    detector_->setFrequencyRange(minHz, maxHz);
    clearHold();
}

void Pipeline::setInstrument(const std::string& name) {
    FrequencyRange r = instrumentPreset(name);
    minHz_ = r.minHz;
    maxHz_ = r.maxHz;
    detector_->setFrequencyRange(r.minHz, r.maxHz);
    clearHold();
}

void Pipeline::setTuning(const std::string& name) {
    stringMatcher_.setTuning(name.empty() ? nullptr : tuningPreset(name));
}

void Pipeline::setTemperament(const std::string& name) {
    noteMapper_.setTemperament(name);
}

void Pipeline::setPostProcessorConfig(PostProcessor::Config cfg) {
    postProcessor_.setConfig(cfg);
    clearHold();
}

void Pipeline::setSpectralRefinement(bool enabled) {
    refine_ = enabled;
}

void Pipeline::setNoteHold(NoteHold hold) {
    noteHold_ = hold;
    clearHold();
}

void Pipeline::clearHold() {
    heldFrequency_ = 0.0f;
    missedFrames_  = 0;
}

void Pipeline::registerMiss() {
    if (++missedFrames_ > noteHold_.maxMissedFrames) resetTracking();
}

bool Pipeline::isMainsHum(float hz) const {
    for (float mains : {50.0f, 60.0f, 100.0f, 120.0f}) {
        if (std::fabs(1200.0f * std::log2(hz / mains)) <= kHumCents) return true;
    }
    return false;
}

void Pipeline::resetTracking() {
    if (heldFrequency_ > 0.0f) fadedHz_ = heldFrequency_;
    postProcessor_.reset();
    refiner_.resetString();
    unrefinedFrames_ = 0;
    fineOverCoarse_ = 0.0f;
    clearHold();
}

void Pipeline::setHpfCutoff(float hz) {
    hpf_ = BiquadHpf(sampleRate_, hz);
    workPrimed_ = false; // the kept frame was filtered at the old cutoff
}

void Pipeline::setOnsetDetectionEnabled(bool enabled) {
    onsetDetector_.setEnabled(enabled);
    if (!enabled) onsetDetector_.reset();
}

void Pipeline::setOnsetConfig(OnsetDetector::Config cfg) {
    onsetDetector_.setConfig(cfg);
}

float Pipeline::calculateRmsDb(const float* input, int n) const {
    return 20.0f * std::log10(std::max(calculateRmsLinear(input, n), kMinLinear));
}

float Pipeline::calculateRmsLinear(const float* input, int n) const {
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        sum += input[i] * input[i];
    }
    return std::sqrt(sum / static_cast<float>(n));
}
