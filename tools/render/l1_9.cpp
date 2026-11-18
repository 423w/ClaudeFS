// L1.9 scenarios: one-pole and biquad filters, measured.
//
//   (console)                         one-pole: requested cutoff vs actual -3 dB point, exp vs naive coefficient
//   l1.9_ir_<type>.wav + .csv         impulse responses of each RBJ type (1 kHz, Q 2, +/-9 dB) and their computed
//                                     response; tools/analyze/plot_response.py overlays measured on computed
//   (console)                         float DF1 vs float TDF2 rounding noise, against a double reference
//   l1.9_bass_sweep.wav               the L1.7 FM bass line through a resonant biquad LP, slow cutoff sweep,
//                                     coefficients updated every sample
//   l1.9_bass_sweep_block64.wav       the same, coefficients updated once per 64 samples ("per block")
//   l1.9_wobble_{df1,tdf2}.wav        fast, deep cutoff modulation (8 Hz, 150 Hz..8 kHz, Q 8): sets up L1.10
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"
#include "score.h"

#include <fss/dsp/biquad.h>
#include <fss/dsp/one_pole.h>
#include <fss/synth/presets.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <random>
#include <vector>

namespace fss::render {

namespace {

constexpr double kFs = kSampleRate;

/// Frequency where |H| first falls below -3.0103 dB (bisection on the computed response).
template <typename Response>
double minus3dbPoint(Response h)
{
    double lo = 0.1, hi = kFs / 2;
    if (std::abs(h(hi)) > std::sqrt(0.5))
        return -1.0; // never reaches -3 dB below Nyquist
    for (int i = 0; i < 80; ++i) {
        const double mid = std::sqrt(lo * hi);
        (std::abs(h(mid)) > std::sqrt(0.5) ? lo : hi) = mid;
    }
    return lo;
}

void onePoleTable()
{
    std::printf("  one-pole LP: requested fc -> actual -3 dB point\n");
    std::printf("    %8s  %22s  %26s\n", "fc (Hz)", "a = 1-exp(-2pi fc/fs)", "a = 2pi fc/fs (naive)");
    for (double fc : {20.0, 100.0, 1000.0, 4000.0, 8000.0, 12000.0, 16000.0}) {
        const double aExp = dsp::onePoleCoefficient(fc, kFs);
        const double aLin = 2.0 * std::numbers::pi * fc / kFs;
        auto resp = [](double a) {
            return [a](double hz) {
                const auto z1 = std::polar(1.0, -2.0 * std::numbers::pi * hz / kFs);
                return a / (1.0 - (1.0 - a) * z1);
            };
        };
        const double fExp = minus3dbPoint(resp(aExp));
        char lin[64];
        if (aLin >= 2.0)
            std::snprintf(lin, sizeof lin, "a=%.2f: UNSTABLE", aLin);
        else if (const double f = minus3dbPoint(resp(aLin)); f < 0)
            std::snprintf(lin, sizeof lin, "a=%.2f: never -3 dB", aLin);
        else
            std::snprintf(lin, sizeof lin, "%.1f Hz (%+.1f %%)", f, 100.0 * (f / fc - 1.0));
        if (fExp < 0)
            std::printf("    %8.0f  %22s  %26s\n", fc, "never -3 dB", lin);
        else
            std::printf("    %8.0f  %13.1f Hz (%+5.1f %%)  %26s\n", fc, fExp, 100.0 * (fExp / fc - 1.0), lin);
    }
}

void impulseResponses(const std::filesystem::path& dir)
{
    struct T {
        dsp::BiquadType type;
        const char* name;
    };
    constexpr T kTypes[] = {{dsp::BiquadType::LowPass, "lowpass"},   {dsp::BiquadType::HighPass, "highpass"},
                            {dsp::BiquadType::BandPass, "bandpass"}, {dsp::BiquadType::Notch, "notch"},
                            {dsp::BiquadType::Peak, "peak"},         {dsp::BiquadType::LowShelf, "lowshelf"},
                            {dsp::BiquadType::HighShelf, "highshelf"}};
    std::printf("  biquad impulse responses (1 kHz, Q 2, gain +9 dB for peak/shelves):\n");
    for (const T& t : kTypes) {
        const auto c = dsp::designBiquad(t.type, kFs, 1000.0, 2.0, 9.0);
        dsp::Biquad f;
        f.setCoeffs(c);
        std::vector<float> h(1 << 14);
        for (std::size_t n = 0; n < h.size(); ++n)
            h[n] = f.tick(n == 0 ? 1.0f : 0.0f);
        // Write raw (PCM float can hold the >1 first sample of a +9 dB shelf; nobody listens to this file).
        renderMono(dir / (std::string("l1.9_ir_") + t.name + ".wav"), h);
        std::ofstream csv(dir / (std::string("l1.9_ir_") + t.name + ".csv"));
        csv << "hz,db,phase_deg\n";
        for (int i = 0; i <= 400; ++i) {
            const double hz = 10.0 * std::pow(2300.0, i / 400.0); // 10 Hz .. 23 kHz
            const auto H = dsp::responseAt(c, hz, kFs);
            csv << hz << ',' << 20.0 * std::log10(std::abs(H) + 1e-300) << ','
                << std::arg(H) * 180.0 / std::numbers::pi << '\n';
        }
        std::printf("    %-9s pole radius %.5f, b = [%+.5f %+.5f %+.5f], a = [1 %+.5f %+.5f]\n", t.name,
                    dsp::poleRadius(c), c.b0, c.b1, c.b2, c.a1, c.a2);
    }
}

/// Rounding noise of float DF1 / TDF2 vs a double TDF2 on the same coefficients, white-noise input.
void precisionTable()
{
    // Two different errors hide in "a float biquad":
    //   coefficient quantization: the float coefficients describe a slightly DIFFERENT filter (poles moved);
    //   state rounding noise:     every multiply-add rounds, and the feedback recirculates that error.
    // To separate them, the middle reference runs in double but with the coefficients rounded to float.
    std::printf("  float biquad error vs a double reference (white noise in; error RMS re output RMS):\n");
    std::printf("    %-34s %14s %14s %14s\n", "", "coef quant.", "DF1 state", "TDF2 state");
    for (double fc : {20.0, 200.0, 2000.0}) {
        const auto c = dsp::designBiquad(dsp::BiquadType::LowPass, kFs, fc, std::numbers::sqrt2 / 2);
        auto rounded = [](double v) { return static_cast<double>(static_cast<float>(v)); };
        const dsp::BiquadCoeffs cq{rounded(c.b0), rounded(c.b1), rounded(c.b2), rounded(c.a1), rounded(c.a2)};
        dsp::BiquadDf1<float> df1;
        dsp::BiquadTdf2<float> tdf2;
        dsp::BiquadTdf2<double> exact, quantized;
        df1.setCoeffs(c);
        tdf2.setCoeffs(c);
        exact.setCoeffs(c);
        quantized.setCoeffs(cq);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
        double sig = 0, eq = 0, e1 = 0, e2 = 0;
        for (int n = 0; n < 10 * kSampleRate; ++n) {
            const float x = noise(rng);
            const double r = exact.tick(x), q = quantized.tick(x);
            const double dq = q - r, d1 = df1.tick(x) - q, d2 = tdf2.tick(x) - q;
            if (n > kSampleRate) { // skip the start-up transient
                sig += r * r;
                eq += dq * dq;
                e1 += d1 * d1;
                e2 += d2 * d2;
            }
        }
        char label[64];
        std::snprintf(label, sizeof label, "LP %5.0f Hz (pole radius %.5f)", fc, dsp::poleRadius(c));
        std::printf("    %-34s %11.1f dB %11.1f dB %11.1f dB\n", label, 10.0 * std::log10(eq / sig),
                    10.0 * std::log10(e1 / sig), 10.0 * std::log10(e2 / sig));
    }
}

/// Filter `in` with a biquad LP whose cutoff follows cutoffAt(n). Coefficients are redesigned every
/// `updateEvery` samples (1 = every sample). Returns the output.
template <typename Filter, typename CutoffFn>
std::vector<float> sweep(const std::vector<float>& in, CutoffFn cutoffAt, double q, int updateEvery)
{
    Filter f;
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) {
        if (n % static_cast<std::size_t>(updateEvery) == 0)
            f.setCoeffs(dsp::designBiquad(dsp::BiquadType::LowPass, kFs, cutoffAt(n), q));
        out[n] = static_cast<float>(f.tick(in[n]));
    }
    return out;
}

double rmsDb(const std::vector<float>& x)
{
    double s = 0;
    for (float v : x)
        s += static_cast<double>(v) * v;
    return 10.0 * std::log10(s / static_cast<double>(x.size()) + 1e-30);
}

double peakOf(const std::vector<float>& x)
{
    double p = 0;
    for (float v : x)
        p = std::fmax(p, std::abs(static_cast<double>(v)));
    return p;
}

} // namespace

void renderL1_9(const std::filesystem::path& dir)
{
    onePoleTable();
    impulseResponses(dir);
    precisionTable();

    // The L1.7 FM bass line (mono: take the left channel), brighter than usual so the filter has work to do.
    synth::Patch bassPatch = synth::presets::fmBass();
    bassPatch.ops[2].level = 3.5f;
    const StereoBuffer bass =
        renderWithEngine(bassPatch, loadScore(std::filesystem::path(FSS_SCORES_DIR) / "l1_7_bass.txt"), {.tailMs = 500.0});
    std::vector<float> in = bass.left;
    const double len = static_cast<double>(in.size());

    // Slow sweep: 150 Hz -> 6 kHz -> 150 Hz (exponential, one up-and-down over the whole line), Q 4.
    auto slow = [len](std::size_t n) {
        const double x = static_cast<double>(n) / len;           // 0..1
        const double tri = 1.0 - std::abs(2.0 * x - 1.0);        // 0..1..0
        return 150.0 * std::pow(40.0, tri);
    };
    const auto perSample = sweep<dsp::Biquad>(in, slow, 4.0, 1);
    const auto perBlock = sweep<dsp::Biquad>(in, slow, 4.0, 64);
    renderMono(dir / "l1.9_bass_sweep.wav", perSample);
    renderMono(dir / "l1.9_bass_sweep_block64.wav", perBlock);
    std::vector<float> diff(in.size());
    for (std::size_t n = 0; n < in.size(); ++n)
        diff[n] = perBlock[n] - perSample[n];
    std::printf("  slow sweep: per-sample RMS %.1f dB; per-64 minus per-sample: %.1f dB\n", rmsDb(perSample), rmsDb(diff));

    // Fast, deep wobble: 8 Hz, 150 Hz .. 8 kHz, Q 8. Per-sample updates, both structures.
    auto wobble = [](std::size_t n) {
        const double ph = 2.0 * std::numbers::pi * 8.0 * static_cast<double>(n) / kFs;
        return 150.0 * std::pow(8000.0 / 150.0, 0.5 + 0.5 * std::sin(ph));
    };
    for (float& v : in)
        v *= 0.3f; // Q 8 boosts the resonance ~18 dB: leave headroom so the WAV doesn't clip
    const auto w1 = sweep<dsp::BiquadDf1<float>>(in, wobble, 8.0, 1);
    const auto w2 = sweep<dsp::BiquadTdf2<float>>(in, wobble, 8.0, 1);
    const auto w2b = sweep<dsp::BiquadTdf2<float>>(in, wobble, 8.0, 64);
    renderMono(dir / "l1.9_wobble_df1.wav", w1);
    renderMono(dir / "l1.9_wobble_tdf2.wav", w2);
    renderMono(dir / "l1.9_wobble_tdf2_block64.wav", w2b);
    std::vector<float> d12(in.size()), d2b(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) {
        d12[n] = w1[n] - w2[n];
        d2b[n] = w2b[n] - w2[n];
    }
    std::printf("  wobble: peak DF1 %.3f, TDF2 %.3f (input peak %.3f); DF1-TDF2 diff %.1f dB, block64-TDF2 diff %.1f dB "
                "(re output %.1f dB)\n",
                peakOf(w1), peakOf(w2), peakOf(in), rmsDb(d12), rmsDb(d2b), rmsDb(w2));
}

} // namespace fss::render
