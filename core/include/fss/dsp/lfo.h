// fss/dsp/lfo.h: the low-frequency oscillator (L1.11). A slow oscillator whose output moves *parameters*
// (pitch, cutoff, FM index) instead of being heard directly.
//
// It reuses the Phasor (composition: an Lfo HAS a Phasor; it isn't one), so it inherits D5's exact,
// wrap-for-free uint32 phase. The shapes are plain functions of the phase t in [0, 1):
//
//   Sine          sin(2πt)                     starts at 0, rising
//   Triangle      0 → +1 → 0 → −1 → 0          starts at 0, rising (so sine and triangle start alike)
//   SawUp         −1 → +1, then jumps back     ramps
//   SawDown       +1 → −1, then jumps back
//   Square        +1 for the first half, −1 for the second
//   SampleHold    a new random value at the start of each cycle, held flat ("random steps")
//   SmoothRandom  glides from one random value to the next over each cycle (cosine-shaped: no corners)
//
// Output is BIPOLAR, −1..+1. Each route scales it into its own unit (semitones, octaves, %), so "depth"
// always reads in the unit you hear (see patch.h).
//
// Aliasing? An LFO's sharp edges (saw, square, S&H) are *modulation* steps, not audio: a pitch step under a
// continuous phase doesn't click, and the SVF takes cutoff steps without clicking (L1.10). So no PolyBLEP here.
//
// Delay + fade-in (for vibrato that "blooms", like a violinist's): output gain is 0 for delayMs after
// restart(), then ramps linearly to 1 over fadeMs. The gain is exposed separately (fadeGain()) so a route
// can apply it to part of the depth only (the mod wheel's vibrato is immediate; the patch's is delayed).
#pragma once

#include <fss/dsp/phasor.h>
#include <fss/dsp/sine.h>
#include <fss/util/random.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace fss::dsp {

enum class LfoShape : std::uint8_t { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
inline constexpr int kNumLfoShapes = 7;

class Lfo {
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        phasor_.prepare(sampleRate);
    }

    void setRate(double hz) noexcept { phasor_.setFrequency(hz); }
    void setShape(LfoShape s) noexcept { shape_ = s; }

    /// Delay and fade-in, in ms (take effect at the next restart()).
    void setDelayFade(float delayMs, float fadeMs) noexcept
    {
        delaySamples_ = static_cast<std::uint32_t>(std::max(0.0f, delayMs) * 0.001 * sampleRate_);
        fadeStep_ = fadeMs > 0.0f ? static_cast<float>(1.0 / (static_cast<double>(fadeMs) * 0.001 * sampleRate_)) : 1.0f;
    }

    /// Restart the cycle at `start` (0 = key sync; another LFO's phase = "free running") and the delay/fade.
    /// `seed` picks the random sequence for S&H / smooth random (voices pass different seeds).
    void restart(Phase start, std::uint32_t seed) noexcept
    {
        phasor_.reset(start);
        last_ = start; // so the first tick isn't mistaken for a wrap
        rng_.seed(seed);
        held_ = rng_.bipolar();
        next_ = rng_.bipolar();
        elapsed_ = 0;
        fadeGain_ = delaySamples_ == 0 && fadeStep_ >= 1.0f ? 1.0f : 0.0f;
    }

    /// One sample of the shape, −1..+1 (the fade gain is NOT applied; see fadeGain()).
    [[nodiscard]] float tick() noexcept
    {
        const Phase p = phasor_.tick();
        // A wrap happened if the phase we just returned is "behind" the one before it (unsigned compare).
        // That's the moment S&H draws its next value.
        if (p < last_) {
            held_ = next_;
            next_ = rng_.bipolar();
        }
        last_ = p;
        advanceFade();
        const double t = phaseToCycles(p);
        switch (shape_) {
        case LfoShape::Sine: return sineTable(p);
        case LfoShape::Triangle: return static_cast<float>(t < 0.25 ? 4.0 * t : t < 0.75 ? 2.0 - 4.0 * t : 4.0 * t - 4.0);
        case LfoShape::SawUp: return static_cast<float>(2.0 * t - 1.0);
        case LfoShape::SawDown: return static_cast<float>(1.0 - 2.0 * t);
        case LfoShape::Square: return t < 0.5 ? 1.0f : -1.0f;
        case LfoShape::SampleHold: return held_;
        case LfoShape::SmoothRandom: {
            // Cosine interpolation from held_ to next_ across the cycle: slope 0 at both ends, so
            // consecutive segments join without a corner.
            const float w = 0.5f - 0.5f * std::cos(static_cast<float>(std::numbers::pi * t));
            return held_ + (next_ - held_) * w;
        }
        }
        return 0.0f;
    }

    /// 0 during the delay, then a linear ramp to 1 over the fade time.
    [[nodiscard]] float fadeGain() const noexcept { return fadeGain_; }
    [[nodiscard]] Phase phase() const noexcept { return phasor_.phase(); }

private:
    void advanceFade() noexcept
    {
        if (fadeGain_ >= 1.0f)
            return;
        if (elapsed_ < delaySamples_) {
            ++elapsed_;
            return;
        }
        fadeGain_ = std::min(1.0f, fadeGain_ + fadeStep_);
    }

    Phasor phasor_;
    util::XorShift32 rng_;
    LfoShape shape_ = LfoShape::Sine;
    double sampleRate_ = 48000.0;
    Phase last_ = 0;
    float held_ = 0.0f, next_ = 0.0f;
    std::uint32_t delaySamples_ = 0, elapsed_ = 0;
    float fadeStep_ = 1.0f, fadeGain_ = 1.0f;
};

} // namespace fss::dsp
