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
    float refine(float coarseHz);

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
    double lastDominance() const { return lastDominance_; }
    // Share of the window's energy the measured partials account for (0..1).
    double lastExplained() const { return lastExplained_; }

private:
    bool measure(int length, double hz, double& outHz, double& outMagnitude,
                 double& outSteadiness) const;
    // One attempt at a given window length; false when too few partials are
    // steady in it (the note is younger than the window).
    bool refineWith(int length, double coarseHz, double& outHz);
    const std::vector<double>& windowFor(int length) const;
    int shortestWindowFor(double hz) const;
    int dominantMultiple(int length, double coarseHz, double& outDominance) const;

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
    double lastDominance_ = 0.0;
    double lastExplained_ = 0.0;
};
