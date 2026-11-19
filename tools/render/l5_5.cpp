// L5.5 scenarios: reverb. Schroeder (1962) and Freeverb as reference designs (here only, not in the plugin),
// then CFS's FDN (fx/reverb.h, D17).
//
//   l5.5_ir_{schroeder,freeverb,fdn}.wav    impulse responses (2 s decay settings), for
//              python tools/analyze/rt60.py renders/l5.5/l5.5_ir_*.wav --out renders/l5.5/rt60.png
//   l5.5_ir_fdn_long_mod{0,30}.wav          8 s decay, modulation off/on: python tools/analyze/rt60.py --roughness
//   l5.5_bell_{dry,schroeder,freeverb,fdn}.wav   the Bell preset's notes through each (listen)
//   (console)  predictions 1-3: RT60 (broadband and per band), echo density at 25/50/100/200 ms
//   (console)  prediction 3: FDN RT60 vs the knob, 0.3..10 s; damping per band
//   (console)  prediction 5: L/R correlation; calibration: wet level on noise vs dry
//   (console)  prediction 7: CPU
#include "engine_render.h"
#include "render_util.h"
#include "rt60.h"
#include "scenarios.h"

#include <fss/dsp/delay_line.h>
#include <fss/fx/delay.h>
#include <fss/fx/reverb.h>
#include <fss/synth/presets.h>
#include <fss/util/random.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace fss::render {

namespace {

constexpr double kFs = kSampleRate;

// ---- Schroeder 1962: four parallel combs, two series allpasses ------------------------------------------
class Schroeder {
public:
    explicit Schroeder(double rt60)
    {
        const double combMs[4] = {29.7, 37.1, 41.1, 43.7};
        for (int i = 0; i < 4; ++i) {
            combD_[i] = static_cast<int>(combMs[i] * 0.001 * kFs);
            comb_[i].prepare(static_cast<std::size_t>(combD_[i]) + 2);
            combG_[i] = static_cast<float>(std::pow(10.0, -3.0 * combMs[i] * 0.001 / rt60)); // the RT60 law
        }
        const double apMs[2] = {5.0, 1.7};
        for (int i = 0; i < 2; ++i) {
            apD_[i] = static_cast<int>(apMs[i] * 0.001 * kFs);
            ap_[i].prepare(static_cast<std::size_t>(apD_[i]) + 2);
        }
    }
    float tick(float x)
    {
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i) { // y[n] = x[n] + g·y[n − D]
            const float y = x + combG_[i] * comb_[i].read(combD_[i] - 1.0);
            comb_[i].push(y);
            sum += y;
        }
        float y = 0.25f * sum;
        for (int i = 0; i < 2; ++i) { // Schroeder allpass: v = x + g·v[n − D], out = −g·v + v[n − D]
            const float delayed = ap_[i].read(apD_[i] - 1.0);
            const float v = y + 0.7f * delayed;
            ap_[i].push(v);
            y = -0.7f * v + delayed;
        }
        return y;
    }

private:
    dsp::DelayLine<dsp::NoInterp> comb_[4], ap_[2];
    int combD_[4]{}, apD_[2]{};
    float combG_[4]{};
};

// ---- Freeverb (Jezar at Dreampoint, 2000, public domain), one channel, as in its source ----------------------
class Freeverb {
public:
    Freeverb(float roomSize, float damping, int spread)
    {
        const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}; // at 44.1 kHz
        const int aps[4] = {556, 441, 341, 225};
        const double k = kFs / 44100.0;
        for (int i = 0; i < 8; ++i) {
            combD_[i] = static_cast<int>((combs[i] + spread) * k);
            comb_[i].prepare(static_cast<std::size_t>(combD_[i]) + 2);
        }
        for (int i = 0; i < 4; ++i) {
            apD_[i] = static_cast<int>((aps[i] + spread) * k);
            ap_[i].prepare(static_cast<std::size_t>(apD_[i]) + 2);
        }
        feedback_ = roomSize * 0.28f + 0.7f;
        damp1_ = damping * 0.4f;
        damp2_ = 1.0f - damp1_;
    }
    [[nodiscard]] float feedback() const { return feedback_; }
    [[nodiscard]] double meanCombMs() const
    {
        double s = 0.0;
        for (int d : combD_)
            s += d;
        return s / 8.0 / kFs * 1000.0;
    }
    float tick(float x)
    {
        const float in = x * 0.015f; // Freeverb's "fixedgain"
        float out = 0.0f;
        for (int i = 0; i < 8; ++i) { // lowpass-feedback comb
            const float y = comb_[i].read(combD_[i] - 1.0);
            store_[i] = y * damp2_ + store_[i] * damp1_;
            comb_[i].push(in + store_[i] * feedback_);
            out += y;
        }
        for (int i = 0; i < 4; ++i) { // Freeverb's allpass (not a true allpass: g = 0.5, the input subtracted)
            const float b = ap_[i].read(apD_[i] - 1.0);
            ap_[i].push(out + b * 0.5f);
            out = b - out;
        }
        return out * 3.0f; // its wet scale
    }

private:
    dsp::DelayLine<dsp::NoInterp> comb_[8], ap_[4];
    int combD_[8]{}, apD_[4]{};
    float store_[8]{};
    float feedback_ = 0.84f, damp1_ = 0.2f, damp2_ = 0.8f;
};

fx::ReverbParams fdnParams(float decayS, float dampingPct = 0.0f, float modPct = 30.0f)
{
    return {.enabled = true, .decayS = decayS, .sizePct = 100.0f, .predelayMs = 0.0f, .dampingPct = dampingPct,
            .modPct = modPct, .lowCutHz = 20.0f, .mixPct = 100.0f};
}

StereoBuffer fdnIr(const fx::ReverbParams& p, double seconds)
{
    fx::Reverb rv;
    rv.prepare(kFs, 512);
    rv.setParams(p);
    rv.reset();
    const auto n = static_cast<std::size_t>(seconds * kFs);
    StereoBuffer b{std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f)};
    b.left[0] = b.right[0] = 1.0f;
    for (std::size_t s = 0; s < n; s += 512)
        rv.process({b.left.data() + s, b.right.data() + s, static_cast<int>(std::min<std::size_t>(512, n - s))});
    return b;
}

template <class R>
std::vector<float> monoIr(R& r, double seconds)
{
    std::vector<float> y(static_cast<std::size_t>(seconds * kFs));
    for (std::size_t i = 0; i < y.size(); ++i)
        y[i] = r.tick(i == 0 ? 1.0f : 0.0f);
    return y;
}

void report(const char* name, const std::vector<float>& ir)
{
    const DecayFit f = measureRt60(ir, kFs);
    std::printf("  %-9s RT60 %.2f s (r^2 %.4f)  bands 250/1k/4k/12k: %.2f %.2f %.2f %.2f s  NED 25/50/100/200 ms: %.2f %.2f %.2f %.2f\n",
                name, f.rt60, f.r2, measureRt60Band(ir, kFs, 250).rt60, measureRt60Band(ir, kFs, 1000).rt60,
                measureRt60Band(ir, kFs, 4000).rt60, measureRt60Band(ir, kFs, 12000).rt60,
                echoDensity(ir, static_cast<std::size_t>(0.025 * kFs), 960), echoDensity(ir, static_cast<std::size_t>(0.05 * kFs), 960),
                echoDensity(ir, static_cast<std::size_t>(0.1 * kFs), 960), echoDensity(ir, static_cast<std::size_t>(0.2 * kFs), 960));
}

void compareDesigns(const std::filesystem::path& out)
{
    std::printf("predictions 1-3: three reverbs at a 2 s setting (Freeverb: room 0.84 feedback, damping 0)\n");
    Schroeder sch(2.0);
    const auto irS = monoIr(sch, 3.0);
    report("Schroeder", irS);
    Freeverb fv(0.5f, 0.0f, 0);
    const double fvRt = -3.0 * fv.meanCombMs() * 0.001 / std::log10(fv.feedback());
    const auto irF = monoIr(fv, 3.0);
    report("Freeverb", irF);
    std::printf("            (Freeverb's room 0.5 = feedback %.2f, mean comb %.1f ms: predicted RT60 %.2f s)\n",
                fv.feedback(), fv.meanCombMs(), fvRt);
    const StereoBuffer irD = fdnIr(fdnParams(2.0f), 3.0);
    report("FDN", irD.left);
    renderMono(out / "l5.5_ir_schroeder.wav", irS);
    renderMono(out / "l5.5_ir_freeverb.wav", irF);
    writeStereo(out / "l5.5_ir_fdn.wav", irD);
    writeStereo(out / "l5.5_ir_fdn_long_mod0.wav", fdnIr(fdnParams(8.0f, 0.0f, 0.0f), 9.0));
    writeStereo(out / "l5.5_ir_fdn_long_mod30.wav", fdnIr(fdnParams(8.0f, 0.0f, 30.0f), 9.0));
}

void knobAccuracy()
{
    std::printf("prediction 3: FDN RT60 vs the knob (damping 0, mod 30 %%):\n");
    for (float rt : {0.3f, 1.0f, 2.0f, 5.0f, 10.0f}) {
        const StereoBuffer ir = fdnIr(fdnParams(rt), std::max(1.0, 0.8 * rt) + 0.3);
        const double b = measureRt60(ir.left, kFs).rt60, m = measureRt60Band(ir.left, kFs, 1000).rt60;
        std::printf("  knob %5.1f s: broadband %6.2f s (%+5.1f %%), 1 kHz band %6.2f s (%+5.1f %%)\n", rt, b,
                    100.0 * (b / rt - 1.0), m, 100.0 * (m / rt - 1.0));
    }
    std::printf("prediction 3: damping, 2 s decay: RT60 per band (250 Hz / 1 kHz / 4 kHz / 8 kHz)\n");
    for (float d : {0.0f, 40.0f, 100.0f}) {
        const StereoBuffer ir = fdnIr(fdnParams(2.0f, d), 2.5);
        std::printf("  damping %3.0f %%: %.2f / %.2f / %.2f / %.2f s\n", d, measureRt60Band(ir.left, kFs, 250).rt60,
                    measureRt60Band(ir.left, kFs, 1000).rt60, measureRt60Band(ir.left, kFs, 4000).rt60,
                    measureRt60Band(ir.left, kFs, 8000).rt60);
    }
}

void stereoAndLevel()
{
    const StereoBuffer ir = fdnIr(fdnParams(2.0f), 2.0);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = static_cast<std::size_t>(0.1 * kFs); i < ir.left.size(); ++i) {
        lr += static_cast<double>(ir.left[i]) * ir.right[i];
        ll += static_cast<double>(ir.left[i]) * ir.left[i];
        rr += static_cast<double>(ir.right[i]) * ir.right[i];
    }
    std::printf("prediction 5: FDN tail L/R correlation %.3f\n", lr / std::sqrt(ll * rr));
    // Calibration: 10 s of white noise (both channels the same), wet only, 1.5 s decay: wet RMS vs dry RMS.
    fx::Reverb rv;
    rv.prepare(kFs, 512);
    rv.setParams(fdnParams(1.5f, 40.0f));
    rv.reset();
    util::XorShift32 rng(3);
    std::vector<float> L(10 * static_cast<std::size_t>(kFs)), R;
    for (float& v : L)
        v = 0.25f * rng.bipolar();
    R = L;
    double dry = 0.0;
    for (float v : L)
        dry += static_cast<double>(v) * v;
    for (std::size_t s = 0; s < L.size(); s += 512) // the last block clamped: an overrun corrupts the heap
        rv.process({L.data() + s, R.data() + s, static_cast<int>(std::min<std::size_t>(512, L.size() - s))});
    double wet = 0.0;
    for (std::size_t i = L.size() / 2; i < L.size(); ++i)
        wet += 0.5 * (static_cast<double>(L[i]) * L[i] + static_cast<double>(R[i]) * R[i]);
    std::printf("calibration: 1.5 s reverb on noise, wet vs dry RMS: %+.1f dB (kOutGain %.2f)\n",
                10.0 * std::log10((wet / (L.size() / 2)) / (dry / L.size())), fx::Reverb::kOutGain);
}

void bells(const std::filesystem::path& out)
{
    std::vector<ScoreNote> notes;
    const std::uint8_t keys[] = {72, 79, 76, 84, 67};
    for (int i = 0; i < 5; ++i)
        notes.push_back({.startMs = i * 1100.0, .key = keys[i], .velocity = 110, .durationMs = 500.0});
    const StereoBuffer dry = renderWithEngine(synth::presets::bell(), notes, {.tailMs = 4000.0});
    writeStereo(out / "l5.5_bell_dry.wav", dry);
    // The references are mono-in/mono-out: 70 % dry + 30 % wet, by hand.
    auto through = [&](auto&& rev, const char* file) {
        StereoBuffer b = dry;
        for (std::size_t i = 0; i < b.left.size(); ++i) {
            const float w = rev.tick(0.5f * (dry.left[i] + dry.right[i]));
            b.left[i] = 0.84f * dry.left[i] + 0.55f * w; // equal power at 30 %
            b.right[i] = 0.84f * dry.right[i] + 0.55f * w;
        }
        writeStereo(out / file, b);
    };
    through(Schroeder(2.5), "l5.5_bell_schroeder.wav");
    through(Freeverb(0.6f, 0.4f, 0), "l5.5_bell_freeverb.wav");
    synth::Patch p = synth::presets::bell();
    p.fx.reverb = {.enabled = true, .decayS = 2.5f, .predelayMs = 15.0f, .mixPct = 30.0f};
    writeStereo(out / "l5.5_bell_fdn.wav", renderWithEngine(p, notes, {.tailMs = 4000.0}));
}

void cost()
{
    fx::Reverb rv;
    rv.prepare(kFs, 256);
    rv.setParams(fdnParams(2.0f, 40.0f));
    rv.reset();
    util::XorShift32 rng(7);
    std::vector<float> L(10 * static_cast<std::size_t>(kFs)), R;
    for (float& v : L)
        v = 0.3f * rng.bipolar();
    R = L;
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t s = 0; s + 256 <= L.size(); s += 256)
        rv.process({L.data() + s, R.data() + s, 256});
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    std::printf("prediction 7: CPU reverb %.1f ns per stereo sample (%.2f %% of one core at 48 kHz)\n",
                ns / static_cast<double>(L.size()), ns / static_cast<double>(L.size()) * 48000.0 * 1e-7);
    // The L5.3 open item: what an FX slot's sleep (a reset) costs on the audio thread at 192 kHz, now that the
    // delay lines clear lazily. Median of 200 resets, each after the buffers were filled with signal.
    auto resetCost = [](auto& fx, const char* name) {
        std::vector<double> t;
        std::vector<float> a(4096, 0.1f), b(4096, 0.1f);
        for (int k = 0; k < 200; ++k) {
            for (int j = 0; j < 4; ++j)
                fx.process({a.data(), b.data(), 4096});
            const auto t0 = std::chrono::steady_clock::now();
            fx.reset();
            t.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        }
        std::nth_element(t.begin(), t.begin() + 100, t.end());
        std::printf("reset at 192 kHz: %-7s %6.1f us\n", name, t[100]);
    };
    fx::StereoDelay delay;
    delay.prepare(192000.0, 4096);
    delay.setParams({.enabled = true});
    resetCost(delay, "delay");
    fx::Reverb big;
    big.prepare(192000.0, 4096);
    big.setParams(fdnParams(2.0f));
    resetCost(big, "reverb");
}

} // namespace

void renderL5_5(const std::filesystem::path& outDir)
{
    compareDesigns(outDir);
    knobAccuracy();
    stereoAndLevel();
    cost();
    bells(outDir);
}

} // namespace fss::render
