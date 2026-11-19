// fss/fx/reverb.h: an 8-line feedback delay network with an input diffuser (L5.5, D17). [SIGNALSMITH-REVERB],
// [JOS-PASP] "Feedback Delay Networks", [VALHALLA].
//
//   in ─► pre-delay ─► low cut ─► DIFFUSER (4 stages) ──┬──────────────── early ──────────────┐
//                                                        └─(+)─► 8 delay lines ──┬── late ───(+)─► out (×2)
//                                                           ▲                     │
//                                                           └── Householder ◄─ decay gain + damping ◄┘
//
// THE IDEA. A room is a huge number of echoes that arrive ever faster and ever quieter. Two jobs:
//   DENSITY. Echoes must pile up fast enough to blur into a smooth wash (by ~50 ms). The DIFFUSER does that before
//     the loop: 8 channels, each through its own short delay, then shuffled, some flipped in polarity, and mixed
//     by an 8×8 Hadamard matrix (every output = ± every input, /√8: energy preserved). One stage turns 1 echo
//     into 8; four stages into 8⁴ = 4096 paths, all within ~60 ms. (Schroeder's allpasses do the same job one
//     echo at a time; Freeverb needs 8 combs and 4 allpasses for less: renders in fss_render l5.5.)
//   DECAY. The 8 delay lines (40–100 ms at size 100 %) feed back into each other through a HOUSEHOLDER matrix,
//     H = I − (2/N)·11ᵀ: orthogonal (it loses no energy by itself, so nothing rings or dies by accident), and it
//     costs one sum. Every pass through line i also multiplies by
//         gᵢ = 10^(−3·dᵢ / (RT60·fs))       (dᵢ in samples: −60 dB after RT60 seconds, whatever dᵢ is)
//     so every line decays at the same rate in dB per second and the whole tail does too: the Decay knob IS the
//     RT60 (tests/test_reverb.cpp: within 1–3 % from 250 Hz to 4 kHz, 0.3 to 10 s). Above ~10 kHz the tail is a
//     little shorter: each pass's cubic read loses a fraction of a dB up there (more with modulation), which
//     pulls the broadband figure ~6 % short. RT60 is quoted at mid frequencies, so that's where the knob is exact.
//
// DAMPING (Jot's absorption filters). Real rooms swallow highs faster. Each line's gain is a one-pole low-pass,
//     H(z) = g·(1 − p)/(1 − p·z⁻¹):   gain g at DC, g·(1 − p)/(1 + p) at Nyquist.
// We choose the Nyquist gain that gives RT60·ratio (ratio 1 → 0.2 as Damping goes 0 → 100 %), which fixes p per
// line. Since each line gets its own p from its own length, the high-frequency decay is uniform too.
//
// MODULATION. A fixed FDN has fixed resonances (modes); on a long tail some of them ring like a metal plate. Each
// line's length wobbles by up to ±0.5 ms (×Mod %) with its own slow LFO (0.1 to 1 Hz), so no mode stays put long
// enough to ring: the cubic read in each line follows it. ±1–2 cents of pitch: a chorus you can't hear as one.
//
// STEREO. The input's L feeds four diffuser channels and R the other four; the outputs are two orthogonal ±1
// patterns over all 8 lines (and over the diffuser, for the early part), so L and R are uncorrelated: wide.
//
// Mix: EQUAL POWER (the reverb is uncorrelated with the dry signal, like the delay). Spill: yes, a switched-off
// reverb rings out. Sizes and pre-delay are glided (a size change bends the tail's pitch briefly, like a tape).
#pragma once

#include <fss/dsp/delay_line.h>
#include <fss/dsp/one_pole.h>
#include <fss/dsp/phasor.h>
#include <fss/dsp/smoother.h>
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>
#include <fss/util/random.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace fss::fx {

class Reverb {
public:
    static constexpr bool kSpillsTail = true;
    static constexpr int kLines = 8, kStages = 4;
    static constexpr double kMinLineMs = 40.0, kMaxLineMs = 100.0; // at size 100 %
    static constexpr double kMinSize = 0.25, kMaxSize = 2.0;
    static constexpr double kMaxModMs = 0.5;
    static constexpr double kMaxPredelayMs = 250.0;
    static constexpr double kStageMs[kStages] = {4.0, 8.0, 16.0, 32.0}; // each diffuser stage's delay range
    static constexpr float kEarlyGain = 0.5f;  // the diffuser's output, heard directly (fills the first 40 ms)
    static constexpr float kOutGain = 1.5f;    // measured: a 1.5 s reverb on noise ≈ the dry level (journal)

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = sampleRate;
        const double ms = 0.001 * sampleRate;
        for (int i = 0; i < kLines; ++i) {
            // Lengths spread exponentially between min and max: no two in a simple ratio, so their echoes never
            // line up into a flutter. (With modulation, "mutually prime" in samples no longer means anything.)
            baseMs_[static_cast<std::size_t>(i)] = kMinLineMs * std::pow(kMaxLineMs / kMinLineMs, i / (kLines - 1.0));
            lines_[static_cast<std::size_t>(i)].prepare(static_cast<std::size_t>(std::ceil((kMaxLineMs * kMaxSize + kMaxModMs) * ms)) + 4);
            lfo_[static_cast<std::size_t>(i)].prepare(sampleRate);
            lfo_[static_cast<std::size_t>(i)].setFrequency(0.1 * std::pow(10.0, i / (kLines - 1.0))); // 0.1 .. 1 Hz
        }
        // The diffuser: stage s, channel i gets a delay spread evenly over the stage's range (i + u)/N, u random
        // but fixed (seeded), so the delays cover the range without clumping. Also a fixed shuffle and signs.
        util::XorShift32 rng(0xD17u);
        for (int s = 0; s < kStages; ++s) {
            Stage& st = diff_[static_cast<std::size_t>(s)];
            int longest = 1;
            for (int i = 0; i < kLines; ++i) {
                const double u = 0.5 * (rng.bipolar() + 1.0f);
                const int d = static_cast<int>(std::max(1.0, std::round(kStageMs[s] * ms * (i + u) / kLines)));
                st.delay[static_cast<std::size_t>(i)] = d;
                st.sign[static_cast<std::size_t>(i)] = rng.bipolar() < 0.0f ? -1.0f : 1.0f;
                longest = std::max(longest, d);
            }
            int frames = 1;
            while (frames <= longest)
                frames <<= 1;
            st.buf.assign(static_cast<std::size_t>(frames) * kLines, 0.0f); // 8 channels interleaved per frame
            st.mask = frames - 1;
        }
        const auto pre = static_cast<std::size_t>(std::ceil(kMaxPredelayMs * ms)) + 2;
        preL_.prepare(pre);
        preR_.prepare(pre);
        for (auto& c : lowCut_)
            c.prepare(sampleRate);
        size_.prepare(sampleRate, 200.0f);
        predelay_.prepare(sampleRate, 50.0f);
        depth_.prepare(sampleRate, 50.0f);
        dry_.prepare(sampleRate, 20.0f);
        wet_.prepare(sampleRate, 20.0f);
        for (int i = 0; i < kLines; ++i) {
            gain_[static_cast<std::size_t>(i)].prepare(sampleRate, 50.0f);
            pole_[static_cast<std::size_t>(i)].prepare(sampleRate, 50.0f);
        }
        setParams(params_);
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines_)
            l.reset();
        for (Stage& st : diff_) { // small (≤ 32 ms × 8 channels): cleared outright
            std::fill(st.buf.begin(), st.buf.end(), 0.0f);
            st.pos = 0;
        }
        preL_.reset();
        preR_.reset();
        for (auto& c : lowCut_)
            c.reset();
        damp_.fill(0.0f);
        for (int i = 0; i < kLines; ++i)
            lfo_[static_cast<std::size_t>(i)].reset(static_cast<dsp::Phase>(0x20000000u * static_cast<unsigned>(i)));
        for (auto* s : {&size_, &predelay_, &depth_, &dry_, &wet_})
            s->reset(s->target());
        for (int i = 0; i < kLines; ++i) {
            gain_[static_cast<std::size_t>(i)].reset(gain_[static_cast<std::size_t>(i)].target());
            pole_[static_cast<std::size_t>(i)].reset(pole_[static_cast<std::size_t>(i)].target());
        }
    }

    void setParams(const ReverbParams& p) noexcept
    {
        params_ = p;
        const double ms = 0.001 * sampleRate_;
        const double size = std::clamp(static_cast<double>(p.sizePct) * 0.01, kMinSize, kMaxSize);
        const double rt = std::clamp(static_cast<double>(p.decayS), 0.05, 60.0);
        const double ratio = 1.0 - 0.8 * std::clamp(static_cast<double>(p.dampingPct), 0.0, 100.0) * 0.01;
        size_.setTarget(static_cast<float>(size));
        predelay_.setTarget(static_cast<float>(std::clamp(static_cast<double>(p.predelayMs), 0.0, kMaxPredelayMs) * ms));
        depth_.setTarget(static_cast<float>(std::clamp(static_cast<double>(p.modPct), 0.0, 100.0) * 0.01 * kMaxModMs * ms));
        for (int i = 0; i < kLines; ++i) {
            // Per line, from its (target) length: the DC gain for RT60, the Nyquist gain for RT60·ratio, and the
            // one-pole's coefficient p that joins them: (1 − p)/(1 + p) = gNyq/gDc  ⇔  p = (1 − r)/(1 + r).
            const double d = baseMs_[static_cast<std::size_t>(i)] * size * ms;
            const double gDc = std::pow(10.0, -3.0 * d / (rt * sampleRate_));
            const double gNyq = std::pow(10.0, -3.0 * d / (rt * ratio * sampleRate_));
            const double r = gNyq / gDc;
            gain_[static_cast<std::size_t>(i)].setTarget(static_cast<float>(gDc));
            pole_[static_cast<std::size_t>(i)].setTarget(static_cast<float>((1.0 - r) / (1.0 + r)));
        }
        for (auto& c : lowCut_)
            c.setCutoff(std::clamp(static_cast<double>(p.lowCutHz), 10.0, 0.45 * sampleRate_));
        const MixGains m = equalPowerMix(p.mixPct * 0.01f);
        dry_.setTarget(m.dry);
        wet_.setTarget(m.wet);
    }

    /// Until the tail is 90 dB down (1.5 × RT60), plus the longest path through the pre-delay, diffuser and a line.
    [[nodiscard]] int tailSamples() const noexcept
    {
        const double ms = 0.001 * sampleRate_;
        const double rt = std::clamp(static_cast<double>(params_.decayS), 0.05, 60.0);
        const double path = (kMaxPredelayMs + 60.0 + kMaxLineMs * kMaxSize) * ms;
        return static_cast<int>(std::ceil(1.5 * rt * sampleRate_ + path));
    }

    void process(StereoBlock b) noexcept
    {
        constexpr float kHalf = 0.5f;                       // upmix: L to 4 channels, R to 4: energy kept
        const float kNorm = 1.0f / std::sqrt(8.0f);         // Hadamard scaling, and the output patterns'
        const double ms = 0.001 * sampleRate_;
        for (int n = 0; n < b.n; ++n) {
            const float size = size_.tick(), pre = predelay_.tick(), depth = depth_.tick();
            const float dry = dry_.tick(), wet = wet_.tick();
            // Pre-delay (whole samples: it's a single echo, no interpolation colour) and the low cut.
            preL_.push(b.L[n]);
            preR_.push(b.R[n]);
            const float inL = lowCut_[0].tickHighPass(preL_.read(pre));
            const float inR = lowCut_[1].tickHighPass(preR_.read(pre));

            // ---- diffuser ----
            std::array<float, kLines> ch{};
            for (int i = 0; i < kLines; ++i)
                ch[static_cast<std::size_t>(i)] = kHalf * ((i & 1) ? inR : inL);
            for (int s = 0; s < kStages; ++s) {
                Stage& st = diff_[static_cast<std::size_t>(s)];
                // One ring per stage, the 8 channels interleaved: write this frame, read each channel d frames ago.
                st.pos = (st.pos + 1) & st.mask;
                float* frame = st.buf.data() + static_cast<std::size_t>(st.pos) * kLines;
                std::copy(ch.begin(), ch.end(), frame);
                std::array<float, kLines> t{};
                for (int i = 0; i < kLines; ++i) {
                    const auto u = static_cast<std::size_t>(i);
                    const auto from = static_cast<std::size_t>((st.pos - st.delay[u]) & st.mask) * kLines + u;
                    // Shuffle: channel i's delayed signal goes to slot (i·3 + s + 1) mod 8 (3 is coprime with 8, so
                    // it's a permutation, a different one per stage), with a fixed sign.
                    t[static_cast<std::size_t>((i * 3 + s + 1) & 7)] = st.sign[u] * st.buf[from];
                }
                hadamard8(t);
                for (float& v : t)
                    v *= kNorm;
                ch = t;
            }

            // ---- the loop ----
            std::array<float, kLines> y{}, z{};
            float sum = 0.0f;
            for (int i = 0; i < kLines; ++i) {
                const auto u = static_cast<std::size_t>(i);
                const float mod = depth * parabolicSine(lfo_[u].tick());
                // Read before push, so the loop delay is (read delay + 1): subtract the 1.
                const double d = baseMs_[u] * ms * static_cast<double>(size) + static_cast<double>(mod) - 1.0;
                y[u] = lines_[u].read(d);
                // Decay and damping: the one-pole with DC gain g and pole p (Jot), its state in damp_.
                const float g = gain_[u].tick(), p = pole_[u].tick();
                damp_[u] = g * (1.0f - p) * y[u] + p * damp_[u];
                z[u] = damp_[u];
                sum += z[u];
            }
            const float house = sum * (2.0f / kLines); // Householder: x − (2/N)·Σx
            for (int i = 0; i < kLines; ++i)
                lines_[static_cast<std::size_t>(i)].push(z[static_cast<std::size_t>(i)] - house + ch[static_cast<std::size_t>(i)]);

            // ---- outputs: two orthogonal ±1 patterns over the lines (late) and over the diffuser (early) ----
            float lateL = 0.0f, lateR = 0.0f, earlyL = 0.0f, earlyR = 0.0f;
            for (int i = 0; i < kLines; ++i) {
                const auto u = static_cast<std::size_t>(i);
                const float sl = (i & 1) ? -1.0f : 1.0f; // + − + − + − + −
                const float sr = (i & 2) ? -1.0f : 1.0f; // + + − − + + − −
                lateL += sl * y[u];
                lateR += sr * y[u];
                earlyL += sl * ch[u];
                earlyR += sr * ch[u];
            }
            const float wl = kOutGain * kNorm * (lateL + kEarlyGain * earlyL);
            const float wr = kOutGain * kNorm * (lateR + kEarlyGain * earlyR);
            b.L[n] = dry * b.L[n] + wet * wl;
            b.R[n] = dry * b.R[n] + wet * wr;
        }
    }

private:
    /// In-place 8-point Hadamard transform (unnormalized): three butterfly passes, additions only. [SIGNALSMITH-REVERB]
    static void hadamard8(std::array<float, kLines>& x) noexcept
    {
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += 2 * h)
                for (int j = i; j < i + h; ++j) {
                    const float a = x[static_cast<std::size_t>(j)], c = x[static_cast<std::size_t>(j + h)];
                    x[static_cast<std::size_t>(j)] = a + c;
                    x[static_cast<std::size_t>(j + h)] = a - c;
                }
    }

    /// sin(2π·phase) ≈ 4x(1 − |x|) with x the phase as −1..1: a parabola per half cycle, smooth enough for a
    /// delay wobble (its slope never jumps) and no sin() per line per sample.
    [[nodiscard]] static float parabolicSine(dsp::Phase p) noexcept
    {
        const float x = static_cast<float>(static_cast<std::int32_t>(p)) * (1.0f / 2147483648.0f);
        return 4.0f * x * (1.0f - std::abs(x));
    }

    ReverbParams params_{};
    double sampleRate_ = 48000.0;
    std::array<double, kLines> baseMs_{};
    std::array<dsp::DelayLine<dsp::CubicInterp>, kLines> lines_{};
    struct Stage {
        std::vector<float> buf; // frames × 8 channels
        int mask = 0, pos = 0;
        std::array<int, kLines> delay{};
        std::array<float, kLines> sign{};
    };
    std::array<Stage, kStages> diff_{};
    std::array<dsp::Phasor, kLines> lfo_{};
    std::array<float, kLines> damp_{};
    dsp::DelayLine<dsp::NoInterp> preL_, preR_;
    std::array<dsp::OnePole, 2> lowCut_{};
    dsp::OnePoleSmoother size_{}, predelay_{}, depth_{}, dry_{}, wet_{};
    std::array<dsp::OnePoleSmoother, kLines> gain_{}, pole_{};
};

static_assert(StereoEffect<Reverb>);

} // namespace fss::fx
