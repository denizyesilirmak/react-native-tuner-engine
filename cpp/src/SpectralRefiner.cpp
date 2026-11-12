#include "SpectralRefiner.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr double kPi = 3.14159265358979323846;

// The analysis window is chosen per note, between two bounds.
//
// Longest: about 170 ms (8192 samples at 48 kHz, a Hann main lobe of
// +-11.7 Hz). Partials of two different open strings are never closer than
// about 27 Hz unless they coincide. Notes under kLowNoteHz get twice that:
// their partials are closer together, and mains hum sits within a few hertz
// of them.
//
// Shortest: the window must still tell the note's own partials apart, so its
// main lobe (2 bins either side) has to stay well inside their spacing.
// Just after the pluck only a short window is steady; the reading starts
// from it and moves to longer ones as the note ages.
constexpr double kWindowSeconds     = 0.17;
constexpr double kLowNoteHz         = 70.0;
constexpr double kPeriodsInShortest = 5.0;
constexpr int    kHopDivisor        = 8; // hop between the two windows = length / 8

constexpr int    kMaxPartials       = 10;
constexpr double kMaxPartialHz      = 5000.0;
// A partial must reach this fraction of the strongest one to be used.
constexpr double kMinRelativeMagnitude = 0.03;
// The two windows must agree in level this well: a partial that is only now
// appearing (the note just started) has no stable phase advance yet.
constexpr double kMinSteadiness     = 0.85;
// The note must already be sounding at the start of the window: its first
// quarter may not be much quieter than its last. A pluck inside the window
// smears every partial, and the Hann taper hides it from the level test above.
constexpr double kMinEarlyToLate    = 0.8;
// A partial this far from the fitted series is being hit by something else.
constexpr double kOutlierCents      = 3.0;
// Share of the vote that keeps a followed note with the partials it has
// been read from.
constexpr double kStayShare         = 0.35;
// The refined pitch may not leave the neighbourhood of the coarse one.
constexpr double kMaxMoveCents      = 60.0;
// Stiffness of real strings; beyond this the fit is following noise.
constexpr double kMaxStiffness      = 1.0e-3;
// The partials must agree this closely (median, cents) once a stiffness is
// applied, for that stiffness to be believed.
constexpr double kStiffnessFitCents = 1.5;
// Partials up to this one vote while the stiffness is unknown.
constexpr int    kUnstretchedVoters = 6;
// A detector estimate further than this from the measured note was too
// rough to judge the octave by.
constexpr double kRecentreCents     = 15.0;
constexpr double kStiffnessSmoothing = 0.25;
// Subharmonic check: how many multiples of the coarse estimate to look at,
// and the share of their energy that must sit on every m-th one for the note
// to be m times higher than the period detector said.
constexpr int    kCombPartials      = 12;
constexpr double kMinDominance      = 0.8;
// A partial counts as sounding from this fraction of the strongest one's
// amplitude (about -30 dB).
constexpr double kPresentFraction   = 0.03;
// How far a partial may sit from the series and still be one of its own.
constexpr double kOwnPartialCents   = 20.0;
// ...and it must stand this far above what lies between the partials. In a
// noisy room a decayed note leaves every bin above 3 % of its strongest
// partial, and noise then passed for a fundamental: a ringing B3 was read as
// B1, a G3 as C2, on a phone recording.
constexpr double kNoiseFactor       = 3.0;
// A peak this many times over its surroundings is as clear as a peak gets.
constexpr double kClearFactor = 20.0;
// Where beside a peak its surroundings are sampled, in bins: just past the
// Hann main lobe (2 bins).
constexpr double kSideBins          = 2.5;
// Octave-below test: a partial half way between the multiples of the coarse
// note counts from this fraction of the strongest partial.
constexpr double kHalfwayFraction   = 0.10;
// ...or from this much smaller fraction when it can be shown to have arrived
// with the pluck.
constexpr double kHalfwayFractionNew = 0.02;
// Looking back before the pluck: window length, the margin kept clear of the
// pluck itself, and how much of a partial's level counts as "already there".
constexpr int    kBeforePluckLength = 4096;
constexpr int    kPluckMargin       = 512;
constexpr double kAlreadyThereFraction = 0.5;
// The partials that arrived with the pluck must hold this share of the
// energy before they alone decide which note was plucked.
constexpr double kMinArrivedShare   = 0.2;
// A single partial with no series is a note only if it accounts for this
// share of the window's energy.
constexpr double kLonePartialExplained = 0.8;
// ...and of a followed note, whose one remaining partial need not be all
// of the sound, only most of it.
constexpr double kLoneFollowExplained = 0.5;

double centsBetween(double a, double b) { return 1200.0 * std::log2(a / b); }

} // namespace

SpectralRefiner::SpectralRefiner(float sampleRate) : sampleRate_(sampleRate) {
    int length = 1024;
    while (length < static_cast<int>(kWindowSeconds * sampleRate_)) length *= 2;
    longest_ = length * 2; // low notes
    history_.assign(static_cast<size_t>(longest_ + longest_ / kHopDivisor), 0.0f);
    for (int n = 256; n <= longest_; n *= 2) {
        std::vector<double> w(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) w[static_cast<size_t>(i)] = 0.5 * (1.0 - std::cos(2.0 * kPi * i / n));
        windows_.push_back(std::move(w));
    }
    partials_.reserve(kMaxPartials);
}

const std::vector<double>& SpectralRefiner::windowFor(int length) const {
    size_t index = 0;
    for (int n = 256; n < length; n *= 2) ++index;
    return windows_[index];
}

void SpectralRefiner::push(const float* samples, int count) {
    if (!samples || count <= 0) return;
    const int capacity = static_cast<int>(history_.size());
    if (count >= capacity) {
        std::memcpy(history_.data(), samples + (count - capacity),
                    static_cast<size_t>(capacity) * sizeof(float));
        filled_ = capacity;
        return;
    }
    std::memmove(history_.data(), history_.data() + count,
                 static_cast<size_t>(capacity - count) * sizeof(float));
    std::memcpy(history_.data() + (capacity - count), samples,
                static_cast<size_t>(count) * sizeof(float));
    filled_ = std::min(capacity, filled_ + count);
}

void SpectralRefiner::resetString() {
    haveStiffness_ = false;
    stiffness_ = 0.0;
    lastF0_ = 0.0;
}

// Frequency of the spectral peak near `hz`, from the phase the peak advances
// by between the older window and the newer one.
bool SpectralRefiner::measure(int length, double hz, double& outHz, double& outMagnitude,
                              double& outSteadiness) const {
    const int hop = length / kHopDivisor;
    const std::vector<double>& window = windowFor(length);
    const double omega = 2.0 * kPi * hz / sampleRate_;
    const double stepRe = std::cos(omega), stepIm = -std::sin(omega);
    double phRe = 1.0, phIm = 0.0;
    double oldRe = 0.0, oldIm = 0.0, newRe = 0.0, newIm = 0.0;
    const float* newer = history_.data() + (history_.size() - static_cast<size_t>(length));
    const float* older = newer - hop;
    for (int n = 0; n < length; ++n) {
        const double w = window[static_cast<size_t>(n)];
        const double a = w * older[n], b = w * newer[n];
        oldRe += a * phRe; oldIm += a * phIm;
        newRe += b * phRe; newIm += b * phIm;
        const double nextRe = phRe * stepRe - phIm * stepIm;
        phIm = phRe * stepIm + phIm * stepRe;
        phRe = nextRe;
    }
    const double oldMag = std::hypot(oldRe, oldIm), newMag = std::hypot(newRe, newIm);
    if (oldMag <= 0.0 || newMag <= 0.0) return false;

    double advance = std::atan2(newIm, newRe) - std::atan2(oldIm, oldRe) - omega * hop;
    advance -= 2.0 * kPi * std::round(advance / (2.0 * kPi));
    outHz = hz + advance * sampleRate_ / (2.0 * kPi * hop);
    outMagnitude = newMag;
    outSteadiness = std::min(oldMag, newMag) / std::max(oldMag, newMag);
    return true;
}

int SpectralRefiner::shortestWindowFor(double hz) const {
    int shortest = 256;
    while (shortest < static_cast<int>(kPeriodsInShortest * sampleRate_ / hz)) shortest *= 2;
    return shortest;
}

bool SpectralRefiner::hasHistoryFor(float coarseHz) const {
    if (coarseHz <= 0.0f) return false;
    const int shortest = shortestWindowFor(coarseHz);
    return filled_ >= shortest + shortest / kHopDivisor;
}

double SpectralRefiner::magnitudeAt(int length, double hz) const {
    const double omega = 2.0 * kPi * hz / sampleRate_;
    const double stepRe = std::cos(omega), stepIm = -std::sin(omega);
    const std::vector<double>& window = windowFor(length);
    const float* newer = history_.data() + (history_.size() - static_cast<size_t>(length));
    double phRe = 1.0, phIm = 0.0, re = 0.0, im = 0.0;
    for (int n = 0; n < length; ++n) {
        const double v = window[static_cast<size_t>(n)] * newer[n];
        re += v * phRe; im += v * phIm;
        const double nextRe = phRe * stepRe - phIm * stepIm;
        phIm = phRe * stepIm + phIm * stepRe;
        phRe = nextRe;
    }
    return std::hypot(re, im);
}

// Room noise is not flat: on a phone in an ordinary room the bins under
// 200 Hz sit only about 20 dB under a plucked string while those above 1 kHz
// are 40 dB down. One noise figure for the whole spectrum let low rumble pass
// for a fundamental (a ringing B3 was read as B1, its "fundamental" at 62 Hz
// being rumble). So each peak is compared with its own surroundings, just
// outside its main lobe and no further than half way to the next partial.
bool SpectralRefiner::standsOut(int length, double hz, double magnitude, double spacingHz) const {
    const double binHz = static_cast<double>(sampleRate_) / length;
    const double offset = std::min(kSideBins * binHz, 0.5 * spacingHz);
    double beside = magnitudeAt(length, hz + offset);
    if (hz - offset > 20.0) beside = std::max(beside, magnitudeAt(length, hz - offset));
    return magnitude >= kNoiseFactor * beside;
}

double SpectralRefiner::prominence(int length, double hz, double magnitude, double spacingHz) const {
    const double binHz = static_cast<double>(sampleRate_) / length;
    const double offset = std::min(kSideBins * binHz, 0.5 * spacingHz);
    double beside = magnitudeAt(length, hz + offset);
    if (hz - offset > 20.0) beside = std::max(beside, magnitudeAt(length, hz - offset));
    return beside > 0.0 ? magnitude / beside : 1.0e6;
}

double SpectralRefiner::magnitudeBefore(int length, double hz, int endOffset) const {
    const int capacity = static_cast<int>(history_.size());
    if (endOffset < 0 || length + endOffset > std::min(capacity, filled_)) return -1.0;
    const double omega = 2.0 * kPi * hz / sampleRate_;
    const double stepRe = std::cos(omega), stepIm = -std::sin(omega);
    const std::vector<double>& window = windowFor(length);
    const float* from = history_.data() + (capacity - endOffset - length);
    double phRe = 1.0, phIm = 0.0, re = 0.0, im = 0.0;
    for (int n = 0; n < length; ++n) {
        const double v = window[static_cast<size_t>(n)] * from[n];
        re += v * phRe; im += v * phIm;
        const double nextRe = phRe * stepRe - phIm * stepIm;
        phIm = phRe * stepIm + phIm * stepRe;
        phRe = nextRe;
    }
    return std::hypot(re, im);
}

// A low string on a phone: the second partial stands 14 to 24 dB over the
// fundamental and everything else, so the period detector may call the note
// an octave up from the first frame. The third and fifth partials are there,
// half way between the multiples of that octave, where a real note an octave
// up has nothing.
//
// But so do other strings: over a ringing low E and B, a plucked E4 has
// E2's second partial at half its frequency and B3's at one and a half, a
// perfect series for an E3 nobody played. What tells them apart is time. The
// partials of the plucked string appeared with the pluck; the others were
// already there. So while the moment before the pluck is still in the
// history, a half-way partial counts only if it was not there then, and it
// may be weak. Later, with nothing to compare against, it has to be strong.
bool SpectralRefiner::soundsAnOctaveBelow(int length, double hz, int samplesSincePluck) const {
    const double topHz = std::min(kMaxPartialHz, 0.45 * sampleRate_);
    double strongest = 0.0;
    for (int k = 1; k <= 6; ++k) {
        if (k * hz > topHz) break;
        double measured = 0.0, magnitude = 0.0, steadiness = 0.0;
        if (measure(length, k * hz, measured, magnitude, steadiness)) strongest = std::max(strongest, magnitude);
    }
    if (strongest <= 0.0) return false;

    // The stretch just before the pluck, if the history still holds it.
    const int beforeLength = std::min(length, kBeforePluckLength);
    const int beforeOffset = samplesSincePluck >= 0 ? samplesSincePluck + kPluckMargin : -1;
    const bool canCompare = beforeOffset >= 0
        && magnitudeBefore(beforeLength, hz, beforeOffset) >= 0.0;
    const double needed = (canCompare ? kHalfwayFractionNew : kHalfwayFraction) * strongest;
    // Window lengths differ, and so do the magnitudes they give for one tone.
    const double scale = static_cast<double>(length) / beforeLength;

    int halfway = 0;
    for (double multiple : {0.5, 1.5, 2.5, 3.5}) {
        const double at = multiple * hz;
        if (at > topHz) break;
        double measured = 0.0, magnitude = 0.0, steadiness = 0.0;
        if (!measure(length, at, measured, magnitude, steadiness)) continue;
        if (magnitude < needed || steadiness < kMinSteadiness
            || std::fabs(measured - at) > 0.015 * at
            || !standsOut(length, at, magnitude, 0.5 * hz)) continue;
        if (canCompare) {
            const double before = magnitudeBefore(beforeLength, measured, beforeOffset) * scale;
            if (before >= kAlreadyThereFraction * magnitude) continue; // another string's
        }
        ++halfway;
    }
    return halfway >= 2;
}

// A period detector answers with the period of the whole sound. Two strings
// a fourth apart (E4 over a ringing B3) repeat together at the period of E2,
// so it says E2, with little conviction, and the frame is thrown away.
//
// The spectrum shows what is wrong with that answer: nothing sounds at E2
// itself. So a correction is considered only when the coarse note's own
// fundamental is missing, and it goes to the lowest multiple that IS
// sounding and carries nearly all the energy.
//
// Energy alone must not decide. A real string often has a second partial far
// stronger than its fundamental (a nylon E4 here puts over 90 % on the even
// ones); judged by energy it would be read an octave up. Its fundamental is
// weak but there, and that is what keeps it where it is.
int SpectralRefiner::dominantMultiple(int length, double coarseHz, int samplesSincePluck,
                                      double& outDominance) const {
    outDominance = 0.0;
    lastByArrival_ = false;
    const double topHz = std::min(kMaxPartialHz, 0.45 * sampleRate_);
    double combMagnitude[kCombPartials + 1] = {0.0};
    double combHz[kCombPartials + 1] = {0.0};
    double combSteadiness[kCombPartials + 1] = {0.0};
    double total = 0.0, strongest = 0.0;
    int count = 0, strongestK = 1;
    for (int k = 1; k <= kCombPartials; ++k) {
        const double hz = k * coarseHz;
        if (hz > topHz) break;
        double measured = 0.0, magnitude = 0.0, steadiness = 0.0;
        if (!measure(length, hz, measured, magnitude, steadiness)) continue;
        combMagnitude[k] = magnitude;
        combHz[k] = measured;
        combSteadiness[k] = steadiness;
        total += magnitude * magnitude;
        if (magnitude > strongest) { strongest = magnitude; strongestK = k; }
        count = k;
    }
    if (total <= 0.0 || count < 4) return 1;

    const double present = kPresentFraction * strongest;

    // A partial of the coarse note itself: there, steady, a real peak, and
    // in tune with the series the strongest partial belongs to. The last
    // matters: a nylon A4 came with a thump of the guitar's body at 222 to
    // 225 Hz, a third of a semitone over half its pitch. Taken for a
    // fundamental it made the note an A3.
    const double seriesHz = combHz[strongestK] / strongestK;
    auto own = [&](int k, bool steady) {
        return combMagnitude[k] >= present
            && (!steady || combSteadiness[k] >= kMinSteadiness)
            && std::fabs(centsBetween(combHz[k] / k, seriesHz)) <= kOwnPartialCents
            && standsOut(length, k * coarseHz, combMagnitude[k], coarseHz);
    };
    // Which note the partials in `energy` (squared magnitudes, 0 where a
    // partial does not count) make: the coarse one (1) or one m times higher.
    auto decide = [&](const double* energy, double energyTotal) {
        // Most of it on the even partials: the octave above, unless the
        // coarse note shows an odd partial of its own above the first. The
        // first alone does not say so. A nylon A4 came with a thump of the
        // guitar's body near 220 Hz, in tune with nothing but loud enough,
        // and was read as an A3 with no third, fifth or seventh partial,
        // which no string has. A banjo's low G, a bass's low E and a nylon
        // E4 have next to no fundamental and their third or fifth is there.
        if (count >= 2 && energy[2] > 0.0
            && standsOut(length, 2 * coarseHz, combMagnitude[2], coarseHz)) {
            double onEven = 0.0;
            for (int k = 2; k <= count; k += 2) onEven += energy[k];
            if (onEven / energyTotal >= kMinDominance) {
                for (int k = 3; k <= count && k <= 7; k += 2) {
                    if (energy[k] > 0.0 && own(k, true)) return 1;
                }
                outDominance = onEven / energyTotal;
                return 2;
            }
        }
        // The coarse note itself is sounding: its fundamental is a real peak.
        if (energy[1] > 0.0 && own(1, false)) return 1;
        for (int m = 3; m <= 6 && m <= count; ++m) {
            // Nothing at that note's fundamental either.
            if (energy[m] <= 0.0
                || !standsOut(length, m * coarseHz, combMagnitude[m], coarseHz)) continue;
            double onMultiples = 0.0;
            for (int k = m; k <= count; k += m) onMultiples += energy[k];
            if (onMultiples / energyTotal >= kMinDominance) {
                int ownBetween = 0;
                for (int k = 1; k <= count; ++k) {
                    if (k % m != 0 && energy[k] > 0.0 && own(k, true)) ++ownBetween;
                }
                if (ownBetween >= 2) return 1;
                outDominance = onMultiples / energyTotal;
                return m;
            }
        }
        return 1;
    };

    // Just after a pluck, with the moment before it still in the history:
    // go by what the pluck brought. Over a low E still ringing, a plucked E4
    // adds energy only at every fourth multiple of E2; the rest was there
    // already and belongs to the old string, fundamental included.
    const int beforeLength = std::min(length, kBeforePluckLength);
    const int beforeOffset = samplesSincePluck >= 0 ? samplesSincePluck + kPluckMargin : -1;
    if (beforeOffset >= 0 && magnitudeBefore(beforeLength, coarseHz, beforeOffset) >= 0.0) {
        const double scale = static_cast<double>(length) / beforeLength;
        double arrived[kCombPartials + 1] = {0.0};
        double arrivedTotal = 0.0;
        for (int k = 1; k <= count; ++k) {
            if (combMagnitude[k] < present) continue;
            const double before = magnitudeBefore(beforeLength, k * coarseHz, beforeOffset) * scale;
            if (before >= kAlreadyThereFraction * combMagnitude[k]) continue;
            arrived[k] = combMagnitude[k] * combMagnitude[k];
            arrivedTotal += arrived[k];
        }
        // Enough of the sound is new for the comparison to mean something.
        if (arrivedTotal >= kMinArrivedShare * total) {
            // Only a raise is taken from this. "Nothing higher arrived" is
            // also what the same string plucked again looks like (its
            // partials were there already), and says nothing about octaves.
            const int multiple = decide(arrived, arrivedTotal);
            if (multiple > 1) {
                lastByArrival_ = true;
                return multiple;
            }
            outDominance = 0.0;
        }
    }

    double all[kCombPartials + 1] = {0.0};
    for (int k = 1; k <= count; ++k) {
        if (combMagnitude[k] >= present) all[k] = combMagnitude[k] * combMagnitude[k];
    }
    return decide(all, total);
}

float SpectralRefiner::refine(float coarseHz, int samplesSincePluck) {
    partials_.clear();
    lastMultiple_ = 1;
    lastDivisor_ = 1;
    lastDominance_ = 0.0;
    lastExplained_ = 0.0;
    if (coarseHz <= 0.0f) return 0.0f;


    const int shortest = shortestWindowFor(coarseHz);
    int length = coarseHz < kLowNoteHz ? longest_ : longest_ / 2;

    // Longest window first; a younger note is only steady in a shorter one.
    for (; length >= shortest; length /= 2) {
        if (filled_ < length + length / kHopDivisor) continue;
        double dominance = 0.0;
        const int multiple = dominantMultiple(length, coarseHz, samplesSincePluck, dominance);
        int divisor = 1;
        double target = coarseHz * multiple;
        if (multiple == 1 && soundsAnOctaveBelow(length, coarseHz, samplesSincePluck)) {
            divisor = 2;
            target = coarseHz / 2.0;
        }
        // A note has a series of partials. One peak alone is accepted only
        // when it is nearly everything in the window (a tuning fork, a
        // whistle); otherwise it is hum, or the one partial a dead note left
        // behind.
        const double topHz = std::min(kMaxPartialHz, 0.45 * sampleRate_);
        const int possible = static_cast<int>(topHz / target);
        double hz = 0.0;
        bool ok = refineWith(length, target, hz, possible >= 2 ? 2 : 1);
        if (!ok && possible >= 2) {
            ok = refineWith(length, target, hz, 1) && lastExplained_ >= kLonePartialExplained;
        }
        if (ok && multiple == 1 && divisor == 1
            && std::fabs(centsBetween(hz, coarseHz)) > kRecentreCents) {
            // The detector was a good way off (a quarter tone, on a string
            // being damped), so the comb laid on its multiples missed the
            // partials and the octave was never really judged. Judge it
            // again from where the note was found.
            double dominanceAgain = 0.0;
            const int again = dominantMultiple(length, hz, samplesSincePluck, dominanceAgain);
            if (again > 1) {
                double higher = 0.0;
                if (refineWith(length, hz * again, higher, 1)) {
                    lastMultiple_ = again;
                    lastDivisor_ = 1;
                    lastDominance_ = dominanceAgain;
                    return static_cast<float>(higher);
                }
                return 0.0f;
            }
        }
        if (ok) {
            lastMultiple_ = multiple;
            lastDivisor_ = divisor;
            lastDominance_ = dominance;
            return static_cast<float>(hz);
        }
    }
    return 0.0f;
}

float SpectralRefiner::refineKnown(float knownHz) {
    partials_.clear();
    lastMultiple_ = 1;
    lastDivisor_ = 1;
    lastDominance_ = 0.0;
    lastExplained_ = 0.0;
    if (knownHz <= 0.0f) return 0.0f;
    const int shortest = shortestWindowFor(knownHz);
    // A pure tone has one partial; anything with more must show two.
    const double topHz = std::min(kMaxPartialHz, 0.45 * sampleRate_);
    const int needed = (2.0 * knownHz <= topHz) ? 2 : 1;
    for (int length = knownHz < kLowNoteHz ? longest_ : longest_ / 2; length >= shortest; length /= 2) {
        if (filled_ < length + length / kHopDivisor) continue;
        double hz = 0.0;
        if (refineWith(length, knownHz, hz, needed, true)) return static_cast<float>(hz);
        // A pure tone is followed by its one partial, on the same terms it
        // was accepted on.
        if (needed > 1 && refineWith(length, knownHz, hz, 1, true)
            && lastExplained_ >= kLoneFollowExplained) {
            return static_cast<float>(hz);
        }
    }
    return 0.0f;
}

bool SpectralRefiner::refineWith(int length, double coarseHz, double& outHz, int minPartials,
                                 bool known) {
    partials_.clear();
    // A different note: its stiffness is not the last one's.
    if (lastF0_ > 0.0 && std::fabs(centsBetween(coarseHz, lastF0_)) > 80.0) resetString();
    const double binHz = static_cast<double>(sampleRate_) / length;

    // Was the note already there when this window began?
    {
        const int span = length + length / kHopDivisor;
        const float* start = history_.data() + (history_.size() - static_cast<size_t>(span));
        const int quarter = span / 4;
        double early = 0.0, late = 0.0;
        for (int i = 0; i < quarter; ++i) {
            early += static_cast<double>(start[i]) * start[i];
            late  += static_cast<double>(start[span - quarter + i]) * start[span - quarter + i];
        }
        if (late <= 0.0 || std::sqrt(early / late) < kMinEarlyToLate) return false;
    }
    const double topHz = std::min(kMaxPartialHz, 0.45 * sampleRate_);
    const double B = haveStiffness_ ? stiffness_ : 0.0;

    double f0 = coarseHz;          // running estimate, tightened as partials come in
    double weightSum = 0.0, weighted = 0.0;
    double strongest = 0.0;

    for (int k = 1; k <= kMaxPartials; ++k) {
        const double stretch = std::sqrt(1.0 + B * k * k);
        const double predicted = k * f0 * stretch;
        if (predicted > topHz) break;

        double hz = 0.0, magnitude = 0.0, steadiness = 0.0;
        if (!measure(length, predicted, hz, magnitude, steadiness)) continue;
        // Off-centre by more than a bin: measure again where the peak is.
        if (std::fabs(hz - predicted) > binHz) {
            double again = 0.0;
            if (!measure(length, hz, again, magnitude, steadiness)) continue;
            hz = again;
        }
        // Too far from where this partial can be: that is another peak.
        if (std::fabs(hz - predicted) > 2.0 * binHz + 0.01 * predicted) continue;

        strongest = std::max(strongest, magnitude);
        partials_.push_back({k, hz, magnitude, steadiness});

        if (steadiness >= kMinSteadiness && magnitude >= kMinRelativeMagnitude * strongest) {
            const int kw = std::min(k, 6);
            const double w = magnitude * magnitude * kw * kw;
            weighted += w * hz / (k * stretch);
            weightSum += w;
            f0 = weighted / weightSum;
        }
    }

    // Keep the partials that are steady and strong enough.
    std::vector<Partial> used;
    std::vector<double> clarity;   // of each used partial, 0..1
    used.reserve(partials_.size());
    clarity.reserve(partials_.size());
    for (const auto& p : partials_) {
        if (p.steadiness < kMinSteadiness || p.magnitude < kMinRelativeMagnitude * strongest) continue;
        const double over = prominence(length, p.frequency, p.magnitude, coarseHz);
        if (over < kNoiseFactor) continue;
        used.push_back(p);
        clarity.push_back(std::min(1.0, over / kClearFactor));
    }
    // One partial is enough when it is the fundamental itself: a pure tone
    // (tuning fork, whistle, a note near the top of the range) has no more.
    if (used.empty()) return false;
    // ...or, for a note already being followed, one of its low partials: a
    // low E on a phone rings on for seconds as little more than its second
    // partial, and that partial still says exactly where the string is.
    if (used.size() == 1 && used.front().k != 1 && !(known && used.front().k <= 3)) return false;
    if (static_cast<int>(used.size()) < minPartials) return false;
    // The series must start low: without the first or second partial the fit
    // has nothing to hold the fundamental with.
    if (used.front().k > 2) return false;

    // Real partials do not sit on a perfect series. On one string the second
    // partial can be 12 cents under it and the fundamental 8 over (the body's
    // own resonances pull the ones near them), while the rest agree to a cent
    // or two. A least-squares fit follows the strays, and with the stiffness
    // free to move it can explain them by bending the whole series: readings
    // 14 cents off came out of that. So both steps below go by the majority.

    // Stiffness: the median of the slopes between every pair of partials
    // (Theil-Sen) of (f_k / k)^2 against k^2, which is a + a*B*k^2.
    double stiffness = B;
    bool trustedStiffness = false;
    {
        // From the second partial up: the first is the one the instrument's
        // body pulls about, and it has the least to say about stiffness.
        std::vector<Partial> upper;
        upper.reserve(used.size());
        for (const auto& p : used) {
            if (p.k >= 2) upper.push_back(p);
        }
        if (upper.size() >= 3 && upper.back().k >= 5) {
            std::vector<double> slopes;
            slopes.reserve(upper.size() * upper.size() / 2);
            for (size_t a = 0; a < upper.size(); ++a) {
                for (size_t b = a + 1; b < upper.size(); ++b) {
                    const double xa = static_cast<double>(upper[a].k) * upper[a].k;
                    const double xb = static_cast<double>(upper[b].k) * upper[b].k;
                    const double ya = (upper[a].frequency / upper[a].k) * (upper[a].frequency / upper[a].k);
                    const double yb = (upper[b].frequency / upper[b].k) * (upper[b].frequency / upper[b].k);
                    slopes.push_back((yb - ya) / (xb - xa));
                }
            }
            std::nth_element(slopes.begin(), slopes.begin() + slopes.size() / 2, slopes.end());
            const double slope = slopes[slopes.size() / 2];
            std::vector<double> intercepts;
            intercepts.reserve(upper.size());
            for (const auto& p : upper) {
                intercepts.push_back((p.frequency / p.k) * (p.frequency / p.k)
                                     - slope * static_cast<double>(p.k) * p.k);
            }
            std::nth_element(intercepts.begin(), intercepts.begin() + intercepts.size() / 2, intercepts.end());
            const double intercept = intercepts[intercepts.size() / 2];
            if (intercept > 0.0) {
                const double frameB = std::clamp(slope / intercept, 0.0, kMaxStiffness);
                // Believed only if the partials then agree. In the frame of
                // a pluck they are all over the place, any stiffness fits,
                // and the one that came out (up to the limit) pushed every
                // reading 10 to 20 cents flat for the next quarter second
                // while it was smoothed away.
                std::vector<double> off;
                off.reserve(upper.size());
                const double f0b = std::sqrt(intercept);
                for (const auto& p : upper) {
                    off.push_back(std::fabs(centsBetween(
                        p.frequency / (p.k * std::sqrt(1.0 + frameB * p.k * p.k)), f0b)));
                }
                std::nth_element(off.begin(), off.begin() + off.size() / 2, off.end());
                if (off[off.size() / 2] <= kStiffnessFitCents) {
                    trustedStiffness = true;
                    stiffness = haveStiffness_
                        ? stiffness_ + kStiffnessSmoothing * (frameB - stiffness_)
                        : frameB;
                }
            }
        }
    }

    // Fundamental: what each partial says it is, the weighted median of
    // those, then the mean of the ones that agree with it.
    struct Vote { double f0; double weight; };
    std::vector<Vote> votes;
    votes.reserve(used.size());
    double totalWeight = 0.0;
    // Until the string's stiffness is known, the high partials cannot vote:
    // each sits sharp of the series by B k^2 / 2, and on a wound G six of
    // them at 8 to 13 cents outvoted the rest for the first two frames.
    int lowVoters = 0;
    for (const auto& p : used) {
        if (p.k <= kUnstretchedVoters) ++lowVoters;
    }
    const bool lowOnly = !trustedStiffness && !haveStiffness_ && lowVoters >= 2;
    for (size_t i = 0; i < used.size(); ++i) {
        const auto& p = used[i];
        if (lowOnly && p.k > kUnstretchedVoters) continue;
        // A vote, not a measure of loudness. On a phone the fundamental of
        // a B3 stood 20 dB over its second and third partials and sat 10
        // cents over them for half a second after the pluck (the guitar's
        // top resonates there and pulls it), while those two agreed to a
        // cent from the first frame on what the string settled to. Weighted
        // by magnitude, the loudest partial is the whole vote. What a vote
        // is worth is how clearly the peak stands over its surroundings: a
        // partial barely out of the room's noise had 322 Hz to say about a
        // 329 Hz string.
        const double weight = std::sqrt(p.magnitude) * std::min(p.k, 6) * clarity[i] * clarity[i];
        votes.push_back({p.frequency / (p.k * std::sqrt(1.0 + stiffness * p.k * p.k)), weight});
        totalWeight += weight;
    }
    std::sort(votes.begin(), votes.end(), [](const Vote& a, const Vote& b) { return a.f0 < b.f0; });
    double running = 0.0, median = votes.front().f0;
    for (const auto& v : votes) {
        running += v.weight;
        if (running >= 0.5 * totalWeight) { median = v.f0; break; }
    }
    // When the partials fall into two camps (a fundamental the guitar's top
    // has pulled 8 cents from the series, on a G string for the whole note),
    // the median changes camp as their levels change and the reading steps
    // by several cents. A note being followed stays with the camp it is in
    // while that camp still holds a fair share of the vote.
    if (known) {
        double near = 0.0;
        for (const auto& v : votes) {
            if (std::fabs(centsBetween(v.f0, coarseHz)) <= kOutlierCents) near += v.weight;
        }
        if (near >= kStayShare * totalWeight) median = coarseHz;
    }
    double agreeing = 0.0, agreeingWeight = 0.0;
    for (const auto& v : votes) {
        if (std::fabs(centsBetween(v.f0, median)) <= kOutlierCents) {
            agreeing += v.weight * v.f0;
            agreeingWeight += v.weight;
        }
    }
    const double fitted = agreeingWeight > 0.0 ? agreeing / agreeingWeight : median;

    if (std::fabs(centsBetween(fitted, coarseHz)) > kMaxMoveCents) return false;

    if (trustedStiffness) {
        stiffness_ = stiffness;
        haveStiffness_ = true;
    }
    // How much of what is in the window these partials account for. Each
    // partial of amplitude A carries A^2/2; under a Hann window its peak
    // magnitude is A * sum(w) / 2.
    {
        const std::vector<double>& window = windowFor(length);
        const float* newer = history_.data() + (history_.size() - static_cast<size_t>(length));
        double sumW = 0.0, sumW2 = 0.0, power = 0.0;
        for (int n = 0; n < length; ++n) {
            const double w = window[static_cast<size_t>(n)];
            sumW += w; sumW2 += w * w;
            power += (w * newer[n]) * (w * newer[n]);
        }
        double partialPower = 0.0;
        for (const auto& p : used) {
            const double amplitude = 2.0 * p.magnitude / sumW;
            partialPower += 0.5 * amplitude * amplitude;
        }
        const double signalPower = power / sumW2;
        lastExplained_ = signalPower > 0.0 ? std::min(1.0, partialPower / signalPower) : 0.0;
    }

    lastPartialCount_ = static_cast<int>(used.size());
    lastF0_ = fitted;
    lastLength_ = length;
    outHz = fitted;
    return true;
}
