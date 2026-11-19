// rt60.h: measure a reverb's decay time and echo density from its impulse response (L5.5).
// Host-side analysis (shared by fss_render's l5.5 scenario and tests/test_reverb.cpp), not core DSP.
// tools/analyze/rt60.py does the same in Python and plots it.
//
// RT60 (Schroeder 1965, "New method of measuring reverberation time"). A single impulse response is noisy: its
// envelope wobbles randomly. Schroeder's trick: integrate the energy BACKWARDS,
//     EDC(t) = ∫_t^∞ h²(τ) dτ        (the energy decay curve: what's left after t)
// which is exactly the average decay of many noise-burst measurements, and smooth. In dB it's (nearly) a straight
// line; fit one between −5 and −35 dB ("T30": skip the direct sound and early reflections at the top, stay above
// the noise floor at the bottom) and extrapolate its slope to 60 dB.
//
// ECHO DENSITY (Abel & Huang 2006, "normalized echo density"). In a window, the fraction of samples further than
// one standard deviation from zero, divided by what Gaussian noise gives (erfc(1/√2) = 0.3173). A few sparse echoes
// → ~0 (most samples are tiny, a few huge). A dense, noise-like tail → 1. Reverbs that "sound like a reverb"
// reach ~1 within a few tens of milliseconds.
#pragma once

#include <fss/dsp/biquad.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace fss::render {

struct DecayFit {
    double rt60 = 0.0;  // seconds, from the T30 slope
    double r2 = 0.0;    // how straight the EDC is over the fit range (1 = perfectly exponential decay)
};

/// RT60 of an impulse response by backward integration and a least-squares line from hiDb down to loDb.
inline DecayFit measureRt60(std::span<const float> h, double sampleRate, double hiDb = -5.0, double loDb = -35.0)
{
    std::vector<double> edc(h.size() + 1, 0.0);
    for (std::size_t i = h.size(); i-- > 0;)
        edc[i] = edc[i + 1] + static_cast<double>(h[i]) * h[i];
    if (edc[0] <= 0.0)
        return {};
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, syy = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < h.size(); ++i) {
        const double db = 10.0 * std::log10(std::max(edc[i] / edc[0], 1e-30));
        if (db > hiDb || db < loDb)
            continue;
        const double t = static_cast<double>(i) / sampleRate;
        sx += t;
        sy += db;
        sxx += t * t;
        sxy += t * db;
        syy += db * db;
        ++n;
    }
    if (n < 10)
        return {};
    const double N = static_cast<double>(n);
    const double slope = (N * sxy - sx * sy) / (N * sxx - sx * sx); // dB per second
    const double r = (N * sxy - sx * sy) / std::sqrt((N * sxx - sx * sx) * (N * syy - sy * sy));
    return {slope < 0.0 ? -60.0 / slope : 0.0, r * r};
}

/// RT60 in one octave band: the IR through an RBJ band-pass (Q √2 ≈ one octave) at centreHz first.
inline DecayFit measureRt60Band(std::span<const float> h, double sampleRate, double centreHz)
{
    dsp::BiquadDf1<double> bp;
    bp.setCoeffs(dsp::designBiquad(dsp::BiquadType::BandPass, sampleRate, centreHz, std::numbers::sqrt2));
    std::vector<float> y(h.size());
    for (std::size_t i = 0; i < h.size(); ++i)
        y[i] = static_cast<float>(bp.tick(static_cast<double>(h[i])));
    return measureRt60(y, sampleRate);
}

/// Normalized echo density in a window of `windowSamples` centred on sample `at`.
inline double echoDensity(std::span<const float> h, std::size_t at, std::size_t windowSamples)
{
    const std::size_t from = at > windowSamples / 2 ? at - windowSamples / 2 : 0;
    const std::size_t to = std::min(h.size(), from + windowSamples);
    if (to <= from + 1)
        return 0.0;
    double s2 = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s2 += static_cast<double>(h[i]) * h[i];
    const double sigma = std::sqrt(s2 / static_cast<double>(to - from));
    std::size_t outside = 0;
    for (std::size_t i = from; i < to; ++i)
        outside += std::abs(static_cast<double>(h[i])) > sigma ? 1 : 0;
    return static_cast<double>(outside) / static_cast<double>(to - from) / 0.3173105078629141;
}

} // namespace fss::render
