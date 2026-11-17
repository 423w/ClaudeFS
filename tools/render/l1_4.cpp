// L1.4 scenarios: two-operator FM (PM). Milestone M1.
//
//   l1.4_fm11_i1.wav, l1.4_fm11_i3.wav   fc = fm = 440 Hz at a fixed index 1 and 3: the prediction check
//   l1.4_bell.wav      Chowning-style bell: 1:1.4, index 10 decaying with the amplitude
//   l1.4_brass.wav     Chowning-style brass: 1:1, index (up to 5) following the amplitude envelope
//   l1.4_clarinet.wav  3:2 (900:600 Hz): odd harmonics of 300 Hz, index 4 settling to 2
//   l1.4_index_sweep.wav   1:1 at 220 Hz, index 0 → 10 over 10 s
//   l1.4_ratio_sweep.wav   fc 220 Hz, ratio 1.0 → 2.0 over 10 s at index 2: beating between integer ratios
//
// The carrier's level is the output amplitude; the modulator's level is the index in radians (operator.h).
#include "render_util.h"
#include "scenarios.h"

#include <fss/synth/operator.h>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace fss::render {

namespace {

struct TwoOp {
    const char* name;
    double carrierHz;
    double ratio;          // fm / fc
    float index;           // peak modulation index, radians (= modulator level)
    dsp::AdsrParams carEnv, modEnv;
    double gateSeconds, totalSeconds;
};

/// Render a 2-op patch. Optional per-sample overrides for the index and the ratio (for sweeps).
std::vector<float> renderTwoOp(const TwoOp& p, const std::function<float(double)>& indexAt = nullptr,
                               const std::function<double(double)>& ratioAt = nullptr)
{
    synth::Operator car, mod;
    for (synth::Operator* op : {&car, &mod})
        op->prepare(kSampleRate);
    car.setEnvelope(p.carEnv);
    mod.setEnvelope(p.modEnv);
    car.setFrequency(p.carrierHz);
    mod.setFrequency(p.carrierHz * p.ratio);
    car.setLevel(0.5f);
    mod.setLevel(p.index);
    car.noteOn();
    mod.noteOn();

    const auto n = static_cast<std::size_t>(std::lround(p.totalSeconds * kSampleRate));
    const auto off = static_cast<std::size_t>(std::lround(p.gateSeconds * kSampleRate));
    std::vector<float> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        if (indexAt)
            mod.setLevel(indexAt(t));
        if (ratioAt)
            mod.setFrequency(p.carrierHz * ratioAt(t));
        if (i == off) {
            car.noteOff();
            mod.noteOff();
        }
        // Modulator first, in the same sample: its output is the carrier's phase offset (radians).
        y[i] = car.tick(mod.tick());
    }
    return y;
}

constexpr dsp::AdsrParams kHold{.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 20.0f};

} // namespace

void renderL1_4(const std::filesystem::path& dir)
{
    // 1. Static spectra for the prediction check (index held constant by a full-sustain envelope).
    for (float index : {1.0f, 3.0f}) {
        const TwoOp p{"fm11", 440.0, 1.0, index, kHold, kHold, 1.0, 1.0};
        renderMono(dir / ("l1.4_fm11_i" + std::to_string(static_cast<int>(index)) + ".wav"), renderTwoOp(p));
    }

    // 2. Bell: inharmonic ratio, long exponential decay, index decaying *with* the amplitude, so the
    //    tone starts metallic-bright and ends almost a pure sine. (After Chowning 1973: fc 200, fm 280, I 10.)
    const dsp::AdsrParams bellEnv{.attackMs = 1.0f, .decayMs = 9000.0f, .sustain = 0.0f, .releaseMs = 9000.0f};
    const dsp::AdsrParams bellIdx{.attackMs = 1.0f, .decayMs = 6000.0f, .sustain = 0.0f, .releaseMs = 6000.0f};
    renderMono(dir / "l1.4_bell.wav", renderTwoOp({"bell", 200.0, 1.4, 10.0f, bellEnv, bellIdx, 8.0, 9.0}));

    // 3. Brass: 1:1 (harmonic, all partials), the index follows the amplitude envelope, so louder = brighter
    //    and the attack "blats" open. Peak index 5.
    const dsp::AdsrParams brassEnv{.attackMs = 60.0f, .decayMs = 300.0f, .sustain = 0.75f, .releaseMs = 150.0f};
    renderMono(dir / "l1.4_brass.wav", renderTwoOp({"brass", 440.0, 1.0, 5.0f, brassEnv, brassEnv, 0.8, 1.2}));

    // 4. Clarinet-ish: 900:600 Hz. Sidebands 900 ± 600k land on 300, 900, 1500, 2100 ...: odd harmonics of
    //    300 Hz, a hollow, square-ish tone. The index starts at 4 and settles to 2 (sustain 0.5).
    const dsp::AdsrParams clarAmp{.attackMs = 70.0f, .decayMs = 100.0f, .sustain = 1.0f, .releaseMs = 120.0f};
    const dsp::AdsrParams clarIdx{.attackMs = 50.0f, .decayMs = 400.0f, .sustain = 0.5f, .releaseMs = 120.0f};
    renderMono(dir / "l1.4_clarinet.wav", renderTwoOp({"clarinet", 900.0, 2.0 / 3.0, 4.0f, clarAmp, clarIdx, 1.0, 1.3}));

    // 5. Experiments: index sweep and ratio sweep (10 s each).
    const TwoOp sweep{"sweep", 220.0, 1.0, 0.0f, kHold, kHold, 10.0, 10.0};
    renderMono(dir / "l1.4_index_sweep.wav",
               renderTwoOp(sweep, [](double t) { return static_cast<float>(t); })); // index = t: 0 → 10
    TwoOp ratio = sweep;
    ratio.index = 2.0f;
    renderMono(dir / "l1.4_ratio_sweep.wav",
               renderTwoOp(ratio, nullptr, [](double t) { return 1.0 + t / 10.0; }));
}

} // namespace fss::render
