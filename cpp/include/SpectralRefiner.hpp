#pragma once

#include <vector>

// Second stage of the frequency estimate. The period detector decides WHICH
// note is sounding; this measures it.
//
// A period estimate looks at the whole waveform, so anything else in it moves
// the answer: another string still ringing shifts it by several cents with a
// fixed sign, and a stiff string's own upper partials (which sit above exact
// multiples) pull it sharp. Here each low partial of the note is measured on
// its own, from the phase advance of its spectral peak between two windows a
// hop apart, on a window long enough to keep neighbouring strings' partials
// out of the peak. The partials are then fitted to the stiff-string law
//   f_k = k * f0 * sqrt(1 + B * k^2)
// and partials that disagree with the fit (hit by another sound) are dropped.
// The result is f0, the pitch of the string's fundamental.
class SpectralRefiner {
public:
    explicit SpectralRefiner(float sampleRate);

    // Append high-passed samples, in order, every frame (silent ones too).
    void push(const float* samples, int count);

    // Forget the string being followed (its stiffness estimate).
    void resetString();

    // Refined fundamental near `coarseHz`, or 0 when the partials cannot be
    // measured reliably (too few, not stationary yet): keep the coarse value.
    // `samplesSincePluck`: how long ago the level last jumped, or -1 when not
    // known. Lets the octave decision tell the partials the pluck brought
    // from those of strings that were already ringing.
    float refine(float coarseHz, int samplesSincePluck = -1);

    // Measure a note already being followed, at `knownHz`: no second-guessing
    // of which note it is, only whether its partials are still there. 0 when
    // fewer than two of them stand out (the note has gone).
    float refineKnown(float knownHz);

    // False right after the engine starts, before enough audio has been
    // pushed to measure a note this low at all.
    bool hasHistoryFor(float coarseHz) const;

    struct Partial {
        int    k;
        double frequency; // measured
        double magnitude;
        double steadiness; // 0..1, the two windows agreeing in level
    };
    const std::vector<Partial>& lastPartials() const { return partials_; }
    double lastStiffness() const { return stiffness_; }
    int lastWindowLength() const { return lastLength_; }

    // When the coarse estimate was a subharmonic of what is really sounding
    // (two strings together share a longer period; so does a note with its
    // own octave below), the note actually measured is `lastMultiple()` times
    // the coarse one, and `lastDominance()` is the share of the partial
    // energy it holds. 1 and 0 when no correction was made.
    int    lastMultiple() const { return lastMultiple_; }
    // 2 when the coarse estimate was an octave above the note (see
    // soundsAnOctaveBelow), else 1.
    int    lastDivisor() const { return lastDivisor_; }
    double lastDominance() const { return lastDominance_; }
    // What the reading stands for. false (default): where the string's
    // series of partials is, which is what the ear hears and does not change
    // as the note dies. true: the period of the waveform, which is what a
    // tuner built on autocorrelation or YIN shows; on a string whose
    // fundamental the instrument's body pulls (a guitar's G, 8 cents) the
    // two differ by 4 to 5 cents.
    void setPeriodReference(bool on) { periodReference_ = on; }
    bool periodReference() const { return periodReference_; }
    // The last multiple was decided from what a pluck brought, not from the
    // spectrum as it stands.
    bool lastMultipleByArrival() const { return lastByArrival_; }
    // Share of the window's energy the measured partials account for (0..1).
    double lastExplained() const { return lastExplained_; }
    // How many partials the last measurement rested on.
    int lastPartialCount() const { return lastPartialCount_; }

private:
    bool measure(int length, double hz, double& outHz, double& outMagnitude,
                 double& outSteadiness) const;
    // One attempt at a given window length; false when too few partials are
    // steady in it (the note is younger than the window).
    // `known`: coarseHz is the note's last reading, not a detector's guess.
    bool refineWith(int length, double coarseHz, double& outHz, int minPartials = 1,
                    bool known = false);
    const std::vector<double>& windowFor(int length) const;
    int shortestWindowFor(double hz) const;
    int dominantMultiple(int length, double coarseHz, int samplesSincePluck,
                         double& outDominance) const;
    // Magnitude of the newer window at `hz` (no frequency estimate).
    double magnitudeAt(int length, double hz) const;
    // A peak of `magnitude` at `hz` is a partial only if it stands clear of
    // what lies just beside it. `spacingHz` is the distance to the next
    // partial of the same series, so the comparison stays between them.
    bool standsOut(int length, double hz, double magnitude, double spacingHz) const;
    // How many times over its own surroundings a peak stands.
    double prominence(int length, double hz, double magnitude, double spacingHz) const;
    // True when the sound is really an octave below `hz`: partials sit half
    // way between its multiples.
    bool soundsAnOctaveBelow(int length, double hz, int samplesSincePluck) const;
    // Magnitude at `hz` over `length` samples ending `endOffset` samples
    // before the newest one; -1 when the history does not reach that far.
    double magnitudeBefore(int length, double hz, int endOffset) const;

    float sampleRate_;
    int   longest_;      // longest analysis window, samples
    std::vector<float>  history_; // longest window + its hop, newest samples last
    std::vector<std::vector<double>> windows_; // Hann, one per power of two
    int   filled_ = 0;

    std::vector<Partial> partials_;
    double stiffness_ = 0.0;      // B, smoothed over the note
    bool   haveStiffness_ = false;
    double lastF0_ = 0.0;
    int    lastLength_ = 0;
    int    lastMultiple_ = 1;
    int    lastDivisor_ = 1;
    double lastDominance_ = 0.0;
    bool   periodReference_ = false;
    // How far each low partial of this string sits from the rest (cents),
    // learned while the rest could be heard; NaN-free: `known` says which.
    static constexpr int kPulledPartials = 4;
    double pullCents_[kPulledPartials + 1] = {0.0};
    bool   pullKnown_[kPulledPartials + 1] = {false};
    mutable bool lastByArrival_ = false;
    double lastExplained_ = 0.0;
    int    lastPartialCount_ = 0;
};
