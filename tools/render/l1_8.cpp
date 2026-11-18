// L1.8 scenarios: classic waveforms and aliasing.
//
//   l1.8_sweep_{saw,square}_naive.wav     100 Hz → 8 kHz exponential sweeps, 6 s, amplitude 0.25, naive
//   l1.8_sweep_{saw,square,tri}_blep.wav  the same sweeps, PolyBLEP / PolyBLAMP
//   l1.8_sweep_saw_additive.wav           ground truth: every harmonic below Nyquist, summed (no aliasing at all)
//   l1.8_tone_<method>_<k>.wav            1 s steady tones at f0 = k·fs/4096 for k = 19, 85, 255, 513
//                                         (222.7, 996.1, 2988.3, 6011.7 Hz), every method
//
// Also prints:
//   - the alias-energy table (exact, via alias_meter.h): dB of non-harmonic energy relative to the total;
//   - the runtime-switch vs template tick timing (meaningful in Release only).
#include "alias_meter.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/waveforms.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

namespace fss::render {

namespace {

using dsp::BlepOscillator;
using dsp::Phasor;
using dsp::Waveform;

constexpr float kLevel = 0.25f; // −12 dBFS peak: a saw is loud; leave headroom for the listener
constexpr std::size_t kPeriodN = 4096;

std::size_t samples(double seconds) { return static_cast<std::size_t>(std::lround(seconds * kSampleRate)); }

/// The methods compared in this lesson. Each turns (phase t in cycles, increment dt) into a sample.
enum class Method { NaiveSaw, BlepSaw, AdditiveSaw, NaiveSquare, BlepSquare, NaiveTri, BlepTri };

struct MethodInfo {
    Method method;
    const char* name;
};

constexpr MethodInfo kMethods[] = {
    {Method::NaiveSaw, "saw_naive"},       {Method::BlepSaw, "saw_blep"},     {Method::AdditiveSaw, "saw_additive"},
    {Method::NaiveSquare, "square_naive"}, {Method::BlepSquare, "square_blep"}, {Method::NaiveTri, "tri_naive"},
    {Method::BlepTri, "tri_blep"},
};

/// Additive saw: 2t − 1 = −(2/π) Σ sin(2πht)/h, summed for every harmonic h·f0 below Nyquist.
/// This is what an ideal anti-aliased saw *is*. Costs one sin per harmonic per sample (200+ at 100 Hz).
/// During a sweep, the top harmonic switches off abruptly when it crosses Nyquist. That's a tiny step at
/// 24 kHz (inaudible, and at most 1/3 of the fundamental's amplitude even at 8 kHz), so no fade is needed for
/// a ground-truth spectrogram. A product-quality additive oscillator would fade harmonics out near Nyquist.
double additiveSaw(double tCycles, double f0)
{
    double sum = 0.0;
    for (int h = 1; h * f0 < 0.5 * kSampleRate; ++h)
        sum += std::sin(2.0 * std::numbers::pi * h * tCycles) / h;
    return -2.0 / std::numbers::pi * sum;
}

/// Render `method` with a per-sample frequency hzAt(n). Phase: a fss Phasor (uint32, D5) for the naive and
/// PolyBLEP methods; a double for additive, where sin(2πh·t) needs t with more than 32 bits once h is large.
template <typename HzAt>
std::vector<float> renderMethod(Method method, std::size_t count, HzAt hzAt)
{
    std::vector<float> x(count);
    Phasor ph;
    ph.prepare(kSampleRate);
    double tAdd = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        const double hz = hzAt(n);
        float v = 0.0f;
        if (method == Method::AdditiveSaw) {
            v = static_cast<float>(additiveSaw(tAdd, hz));
            tAdd += hz / kSampleRate;
            tAdd -= std::floor(tAdd);
        } else {
            ph.setFrequency(hz);
            const double dt = dsp::phaseToCycles(ph.increment());
            const double t = dsp::phaseToCycles(ph.tick());
            switch (method) {
            case Method::NaiveSaw: v = dsp::naiveSaw(t); break;
            case Method::BlepSaw: v = dsp::blepSaw(t, dt); break;
            case Method::NaiveSquare: v = dsp::naiveSquare(t); break;
            case Method::BlepSquare: v = dsp::blepSquare(t, dt); break;
            case Method::NaiveTri: v = dsp::naiveTriangle(t); break;
            case Method::BlepTri: v = dsp::blepTriangle(t, dt); break;
            case Method::AdditiveSaw: break; // handled above
            }
        }
        x[n] = kLevel * v;
    }
    return x;
}

// ---- timing: runtime switch vs template --------------------------------------------------------

volatile int gWaveformSelector = 1; // volatile: the compiler can't know which waveform tick(w) will get

template <typename Tick>
double nsPerSample(Tick tick, std::size_t count, double& sink)
{
    const auto start = std::chrono::steady_clock::now();
    double acc = 0.0;
    for (std::size_t i = 0; i < count; ++i)
        acc += tick();
    const auto stop = std::chrono::steady_clock::now();
    sink += acc; // printed later, so the loop can't be optimized away
    return std::chrono::duration<double, std::nano>(stop - start).count() / static_cast<double>(count);
}

template <Waveform W>
void timeWaveform(const char* name, double& sink)
{
    constexpr std::size_t kCount = 20'000'000;
    BlepOscillator a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setFrequency(440.0);
    b.setFrequency(440.0);
    const Waveform runtimeW = static_cast<Waveform>(gWaveformSelector + static_cast<int>(W) - 1);
    // Run each twice and keep the faster, to soften one-off hiccups (cache warm-up, the OS).
    double tmpl = 1e9, rt = 1e9;
    for (int rep = 0; rep < 2; ++rep) {
        tmpl = std::min(tmpl, nsPerSample([&] { return a.tick<W>(); }, kCount, sink));
        rt = std::min(rt, nsPerSample([&] { return b.tick(runtimeW); }, kCount, sink));
    }
    std::printf("  %-9s template %6.3f ns/sample   runtime switch %6.3f ns/sample   (ratio %.2f)\n", name, tmpl, rt,
                rt / tmpl);
}

/// The worst case for a switch: the waveform changes every sample, so the branch can't be predicted
/// from one sample to the next (and a pattern of 4 is still learnable by modern predictors; this is mostly a
/// curiosity). Compared with calling all four templates in turn.
void timeMixed(double& sink)
{
    constexpr std::size_t kCount = 20'000'000;
    BlepOscillator a, b;
    a.prepare(kSampleRate);
    b.prepare(kSampleRate);
    a.setFrequency(440.0);
    b.setFrequency(440.0);
    std::size_t i = 0;
    const int base = gWaveformSelector - 1;
    const double rt = nsPerSample([&] { return b.tick(static_cast<Waveform>((base + i++) & 3)); }, kCount, sink);
    std::size_t j = 0;
    const double tmpl = nsPerSample([&] {
        switch ((j++) & 3) { // the same rotation, but each case calls a template directly
        case 0: return a.tick<Waveform::Sine>();
        case 1: return a.tick<Waveform::Saw>();
        case 2: return a.tick<Waveform::Square>();
        default: return a.tick<Waveform::Triangle>();
        }
    }, kCount, sink);
    std::printf("  %-9s hand-rotated templates %6.3f ns/sample   runtime switch %6.3f ns/sample\n", "rotating", tmpl,
                rt);
}

} // namespace

void renderL1_8(const std::filesystem::path& dir)
{
    // 1. Sweeps: 100 Hz → 8 kHz, exponential (equal time per octave: 6.32 octaves in 6 s).
    const std::size_t sweepN = samples(6.0);
    const auto sweepHz = [sweepN](std::size_t n) {
        return 100.0 * std::pow(80.0, static_cast<double>(n) / static_cast<double>(sweepN - 1));
    };
    for (Method m : {Method::NaiveSaw, Method::BlepSaw, Method::AdditiveSaw, Method::NaiveSquare, Method::BlepSquare,
                     Method::BlepTri}) {
        const char* name = "";
        for (const auto& info : kMethods)
            if (info.method == m)
                name = info.name;
        // "saw_naive" → "l1.8_sweep_saw_naive.wav"
        renderMono(dir / ("l1.8_sweep_" + std::string(name) + ".wav"), renderMethod(m, sweepN, sweepHz));
    }

    // 2. Steady tones at f0 = k·fs/N (k odd: see alias_meter.h). 1 s each for listening and for
    //    alias_energy.py; the first N samples are measured exactly here.
    constexpr std::size_t kTones[] = {19, 85, 255, 513};
    //    Two numbers per tone: all alias energy, and only the part below 5 kHz (bin 427 = 5004 Hz), where
    //    hearing is sharpest and aliases are most exposed.
    constexpr std::size_t kLowBins = 427;
    std::vector<AliasMeasurement> table[std::size(kMethods)];
    for (std::size_t mi = 0; mi < std::size(kMethods); ++mi) {
        for (std::size_t k : kTones) {
            const double f0 = static_cast<double>(k) * kSampleRate / static_cast<double>(kPeriodN);
            const auto x = renderMethod(kMethods[mi].method, samples(1.0), [f0](std::size_t) { return f0; });
            renderMono(dir / ("l1.8_tone_" + std::string(kMethods[mi].name) + "_" + std::to_string(k) + ".wav"), x);
            table[mi].push_back(measureAliasing(std::span<const float>(x).first(kPeriodN), k, kLowBins));
        }
    }

    std::printf("\nAlias energy, dB re total AC power: all aliases / aliases below 5 kHz (exact periodic DFT, N = %zu)\n",
                kPeriodN);
    std::printf("  %-14s", "method");
    for (std::size_t k : kTones)
        std::printf("  %7.1f Hz (k=%-3zu)", static_cast<double>(k) * kSampleRate / static_cast<double>(kPeriodN), k);
    std::printf("\n");
    for (std::size_t mi = 0; mi < std::size(kMethods); ++mi) {
        std::printf("  %-14s", kMethods[mi].name);
        for (const auto& m : table[mi])
            std::printf("  %7.1f / %7.1f ", m.aliasDb(), m.lowAliasDb());
        std::printf("\n");
    }

    // 3. Timing. Only meaningful in a Release build.
    std::printf("\nTick cost, 440 Hz, 20M samples, best of 2 (Release only is meaningful):\n");
    double sink = 0.0;
    timeWaveform<Waveform::Sine>("sine", sink);
    timeWaveform<Waveform::Saw>("saw", sink);
    timeWaveform<Waveform::Square>("square", sink);
    timeWaveform<Waveform::Triangle>("triangle", sink);
    timeMixed(sink);
    std::printf("  (checksum %.6g, printed so the optimizer must compute it)\n", sink);
}

} // namespace fss::render
