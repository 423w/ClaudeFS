// fss/dsp/adsr.h: ADSR envelope as a state machine (L1.3, D6).
//
//   gate on ─► Attack ──(reaches 1)──► Decay ──(reaches sustain)──► Sustain
//                 │                       │                            │
//                 └───────── gate off from ANY stage ─────────────────► Release ──(reaches 0)──► Idle
//   gate on while Release (or anywhere): Attack again, *from the current level* (no jump, no click)
//
// Each segment is the one-pole "approach a target" step   level = base + level·coef
// aimed slightly *past* where the segment should end (the overshoot), so the curve actually arrives
// in finite time instead of creeping up on its target forever ([EARLEVEL] "Envelope generators").
//
// TIME UNITS (all in ms, all sample-rate independent: coefficients are recomputed in prepare()):
//   attackMs   time from 0 to full scale (1.0). Attack from a higher level arrives sooner.
//   decayMs    a *rate*: the time a fall from 1.0 would take to reach −60 dB. Decay stops at sustain.
//   releaseMs  the same rate definition: from 1.0, the level is at −60 dB after exactly releaseMs and
//              reaches 0 about 11 % later. From a lower level, release takes proportionally less time,
//              like an analog RC envelope. ("Time to −60 dB" is how reverb and envelope times are usually
//              measured, and it's roughly when a note stops being audible.)
//   sustain    a *linear* level in [0, 1] (0.5 = −6 dB). The UI may map a knob onto it differently (L2.4).
// Every time is clamped to ≥ kMinTimeMs = 1 ms: measured in L1.3, a 1 ms attack on a sine starting at
// its peak is already quieter at onset (−46 dB above 2 kHz) than a bare sine starting at zero (−44 dB).
//
// Shapes: attack aims at 1.3 (overshoot ratio 0.3), giving the fast-start, slowing curve of a charging
// capacitor; decay and release aim 0.001 below their end, giving straight-line-in-dB exponential
// falls that finish cleanly at their end level.
//
// State and coefficients are double: a 30 s release at 96 kHz needs coef ≈ 1 − 2.4e-6, which float
// (spacing 6e-8 near 1) can only represent to ~2.5 % (a 30 s release would be off by most of a second).
//
// Real-time safe: no allocation, no exceptions; tick() is one multiply-add, one compare, and a switch.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fss::dsp {

struct AdsrParams {
    float attackMs = 5.0f;
    float decayMs = 300.0f;
    float sustain = 0.7f; // linear level 0..1
    float releaseMs = 300.0f;
};

class Adsr {
public:
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Sustain, Release };

    static constexpr float kMinTimeMs = 1.0f;

    // Usable before prepare() (at 48 kHz): never leave the coefficients at 0, or Attack would never finish.
    Adsr() noexcept { recompute(); }

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        recompute();
    }

    /// Set all four parameters. Safe at any time, even mid-note: the current level never jumps.
    void setParams(const AdsrParams& p) noexcept
    {
        params_ = p;
        recompute();
    }

    /// Silence immediately (voice initialization). Not for musical use: it jumps to 0.
    void reset() noexcept
    {
        stage_ = Stage::Idle;
        level_ = 0.0;
    }

    /// Gate on: start (or restart) the attack from wherever the level is now.
    void noteOn() noexcept { stage_ = Stage::Attack; }

    /// Gate off: release from the current level, whatever stage we're in. Ignored when idle.
    void noteOff() noexcept
    {
        if (stage_ != Stage::Idle)
            stage_ = Stage::Release;
    }

    /// Advance one sample and return the level in [0, 1].
    [[nodiscard]] float tick() noexcept
    {
        switch (stage_) {
        case Stage::Idle:
            break;
        case Stage::Attack:
            level_ = attackBase_ + level_ * attackCoef_;
            if (level_ >= 1.0) {
                level_ = 1.0;
                stage_ = Stage::Decay;
            }
            break;
        case Stage::Decay: {
            const double before = level_;
            level_ = decayBase_ + level_ * fallCoef_[0];
            if (level_ <= sustain_) {
                // Normally we just stepped across sustain from above: land on it (a sub-step correction).
                // If the sustain knob was raised *above* us mid-decay, don't snap up (that would click):
                // the Sustain stage glides there instead.
                if (before >= sustain_)
                    level_ = sustain_;
                stage_ = Stage::Sustain;
            }
            break;
        }
        case Stage::Sustain:
            // Follow sustain-knob changes with a short (≈5 ms) glide instead of jumping to the new level.
            level_ += (sustain_ - level_) * sustainFollow_;
            break;
        case Stage::Release:
            level_ = releaseBase_ + level_ * fallCoef_[1];
            if (level_ <= 0.0) {
                level_ = 0.0;
                stage_ = Stage::Idle;
            }
            break;
        }
        return static_cast<float>(level_);
    }

    /// False once the release has finished. A voice whose amp envelope is inactive can be freed (L1.7).
    [[nodiscard]] bool isActive() const noexcept { return stage_ != Stage::Idle; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] float level() const noexcept { return static_cast<float>(level_); }

private:
    static constexpr double kAttackOvershoot = 0.3;   // aim at 1.3: the analog "charging" curve
    static constexpr double kFallOvershoot = 0.001;   // aim 0.001 past the end: −60 dB, see header

    [[nodiscard]] double samplesFor(float ms) const noexcept
    {
        // Written as !(ms >= min) rather than std::max(ms, min): std::max(NaN, 1) returns NaN, and a NaN
        // coefficient would poison the level forever. This form maps NaN and negatives to the minimum.
        const double t = (ms >= kMinTimeMs) ? static_cast<double>(ms) : static_cast<double>(kMinTimeMs);
        return t * 0.001 * sampleRate_;
    }

    void recompute() noexcept
    {
        // Attack: from 0, level(n) = (1+r)(1 − cⁿ). It reaches 1 when cⁿ = r/(1+r), so
        // c = (r/(1+r))^(1/N) with N = attack time in samples. base = (1+r)(1 − c).
        {
            constexpr double r = kAttackOvershoot;
            attackCoef_ = std::exp(std::log(r / (1.0 + r)) / samplesFor(params_.attackMs));
            attackBase_ = (1.0 + r) * (1.0 - attackCoef_);
        }
        // Decay and release: falling toward (end − r), level(n) = end − r + (L0 − end + r)·cⁿ.
        // Choose c so that a fall from 1 toward 0 is at −60 dB (0.001 = r) after N samples:
        // −r + (1+r)·c^N = r  ⇒  c = (2r/(1+r))^(1/N).
        constexpr double r = kFallOvershoot;
        const double logFall = std::log(2.0 * r / (1.0 + r));
        fallCoef_[0] = std::exp(logFall / samplesFor(params_.decayMs));
        fallCoef_[1] = std::exp(logFall / samplesFor(params_.releaseMs));

        sustain_ = std::isnan(params_.sustain) ? 0.0 : std::clamp(static_cast<double>(params_.sustain), 0.0, 1.0);
        decayBase_ = (sustain_ - r) * (1.0 - fallCoef_[0]);
        releaseBase_ = -r * (1.0 - fallCoef_[1]);

        // One-pole smoothing for sustain changes: time constant 5 ms.
        sustainFollow_ = 1.0 - std::exp(-1.0 / (0.005 * sampleRate_));
    }

    AdsrParams params_{};
    double sampleRate_ = 48000.0;
    Stage stage_ = Stage::Idle;
    double level_ = 0.0;

    double attackCoef_ = 0.0, attackBase_ = 0.0;
    double fallCoef_[2] = {0.0, 0.0}; // [0] decay, [1] release
    double decayBase_ = 0.0, releaseBase_ = 0.0;
    double sustain_ = 0.0, sustainFollow_ = 0.0;
};

} // namespace fss::dsp
