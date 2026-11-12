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
// The refined pitch may not leave the neighbourhood of the coarse one.
constexpr double kMaxMoveCents      = 60.0;
// Stiffness of real strings; beyond this the fit is following noise.
constexpr double kMaxStiffness      = 2.0e-3;
constexpr double kStiffnessSmoothing = 0.25;
// Subharmonic check: how many multiples of the coarse estimate to look at,
// and the share of their energy that must sit on every m-th one for the note
// to be m times higher than the period detector said.
constexpr int    kCombPartials      = 12;
constexpr double kMinDominance      = 0.8;
// A partial counts as sounding from this fraction of the strongest one's
// amplitude (about -30 dB).
constexpr double kPresentFraction   = 0.03;

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
int SpectralRefiner::dominantMultiple(int length, double coarseHz, double& outDominance) const {
    outDominance = 0.0;
    const double topHz = std::min(kMaxPartialHz, 0.45 * sampleRate_);
    double magnitudeAt[kCombPartials + 1] = {0.0};
    double total = 0.0, strongest = 0.0;
    int count = 0;
    for (int k = 1; k <= kCombPartials; ++k) {
        const double hz = k * coarseHz;
        if (hz > topHz) break;
        double measured = 0.0, magnitude = 0.0, steadiness = 0.0;
        if (!measure(length, hz, measured, magnitude, steadiness)) continue;
        magnitudeAt[k] = magnitude;
        total += magnitude * magnitude;
        strongest = std::max(strongest, magnitude);
        count = k;
    }
    if (total <= 0.0 || count < 4) return 1;

    const double present = kPresentFraction * strongest;
    if (magnitudeAt[1] >= present) return 1; // the coarse note itself is sounding

    for (int m = 2; m <= 6 && m <= count; ++m) {
        if (magnitudeAt[m] < present) continue; // nothing at that note's fundamental either
        double onMultiples = 0.0;
        for (int k = m; k <= count; k += m) onMultiples += magnitudeAt[k] * magnitudeAt[k];
        if (onMultiples / total >= kMinDominance) {
            outDominance = onMultiples / total;
            return m;
        }
    }
    return 1;
}

float SpectralRefiner::refine(float coarseHz) {
    partials_.clear();
    lastMultiple_ = 1;
    lastDominance_ = 0.0;
    lastExplained_ = 0.0;
    if (coarseHz <= 0.0f) return 0.0f;


    const int shortest = shortestWindowFor(coarseHz);
    int length = coarseHz < kLowNoteHz ? longest_ : longest_ / 2;

    // Longest window first; a younger note is only steady in a shorter one.
    for (; length >= shortest; length /= 2) {
        if (filled_ < length + length / kHopDivisor) continue;
        double dominance = 0.0;
        const int multiple = dominantMultiple(length, coarseHz, dominance);
        double hz = 0.0;
        if (refineWith(length, coarseHz * multiple, hz)) {
            lastMultiple_ = multiple;
            lastDominance_ = dominance;
            return static_cast<float>(hz);
        }
    }
    return 0.0f;
}

bool SpectralRefiner::refineWith(int length, double coarseHz, double& outHz) {
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
    used.reserve(partials_.size());
    for (const auto& p : partials_) {
        if (p.steadiness >= kMinSteadiness && p.magnitude >= kMinRelativeMagnitude * strongest) {
            used.push_back(p);
        }
    }
    // One partial is enough when it is the fundamental itself: a pure tone
    // (tuning fork, whistle, a note near the top of the range) has no more.
    if (used.empty()) return false;
    if (used.size() == 1 && used.front().k != 1) return false;
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
    if (used.size() >= 3 && used.back().k >= 4) {
        std::vector<double> slopes;
        slopes.reserve(used.size() * used.size() / 2);
        for (size_t a = 0; a < used.size(); ++a) {
            for (size_t b = a + 1; b < used.size(); ++b) {
                const double xa = static_cast<double>(used[a].k) * used[a].k;
                const double xb = static_cast<double>(used[b].k) * used[b].k;
                const double ya = (used[a].frequency / used[a].k) * (used[a].frequency / used[a].k);
                const double yb = (used[b].frequency / used[b].k) * (used[b].frequency / used[b].k);
                slopes.push_back((yb - ya) / (xb - xa));
            }
        }
        std::nth_element(slopes.begin(), slopes.begin() + slopes.size() / 2, slopes.end());
        const double slope = slopes[slopes.size() / 2];
        std::vector<double> intercepts;
        intercepts.reserve(used.size());
        for (const auto& p : used) {
            intercepts.push_back((p.frequency / p.k) * (p.frequency / p.k)
                                 - slope * static_cast<double>(p.k) * p.k);
        }
        std::nth_element(intercepts.begin(), intercepts.begin() + intercepts.size() / 2, intercepts.end());
        const double intercept = intercepts[intercepts.size() / 2];
        if (intercept > 0.0) {
            const double frameB = std::clamp(slope / intercept, 0.0, kMaxStiffness);
            stiffness = haveStiffness_
                ? stiffness_ + kStiffnessSmoothing * (frameB - stiffness_)
                : frameB;
        }
    }

    // Fundamental: what each partial says it is, the weighted median of
    // those, then the mean of the ones that agree with it.
    struct Vote { double f0; double weight; };
    std::vector<Vote> votes;
    votes.reserve(used.size());
    double totalWeight = 0.0;
    for (const auto& p : used) {
        const double weight = p.magnitude * std::min(p.k, 6);
        votes.push_back({p.frequency / (p.k * std::sqrt(1.0 + stiffness * p.k * p.k)), weight});
        totalWeight += weight;
    }
    std::sort(votes.begin(), votes.end(), [](const Vote& a, const Vote& b) { return a.f0 < b.f0; });
    double running = 0.0, median = votes.front().f0;
    for (const auto& v : votes) {
        running += v.weight;
        if (running >= 0.5 * totalWeight) { median = v.f0; break; }
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

    stiffness_ = stiffness;
    haveStiffness_ = true;
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

    lastF0_ = fitted;
    lastLength_ = length;
    outHz = fitted;
    return true;
}
