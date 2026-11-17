// L1.2 scenarios: the phase accumulator.
//
//   l1.2_glide_exp.wav       110 → 880 Hz in 2 s, interpolating *pitch* (MIDI note), Phasor + sineTable
//   l1.2_glide_linhz.wav     the same range, interpolating Hz (sounds uneven: [THINK] #1)
//   l1.2_glide_naive.wav     sin(2π·f(t)·t): the fragile formula. Ends near 2.7 kHz, not 880 Hz
//   l1.2_step_phasor.wav     440 → 660 Hz at t = 1.0011 s with a Phasor: continuous
//   l1.2_step_naive.wav      the same step with the formula: the phase jumps, so the waveform jumps, and it clicks
//   l1.2_chord.wav           A minor triad, three phasors summed, 0.3 each
//   l1.2_sine1k_{table,poly,std}.wav  1 kHz at −6 dBFS from each sine, for spur hunting in the spectrum
//
// Also prints the D5 experiment: float vs double vs uint32 phase accumulators run for one hour of samples.
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/phasor.h>
#include <fss/dsp/sine.h>
#include <fss/util/math.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

namespace fss::render {

namespace {

using dsp::Phase;
using dsp::Phasor;

std::size_t samples(double seconds) { return static_cast<std::size_t>(std::lround(seconds * kSampleRate)); }

/// Run one Phasor through a sine, asking `hzAt(n)` for the frequency at every sample.
template <typename HzAt, typename Sine>
std::vector<float> oscillator(double seconds, float amplitude, HzAt hzAt, Sine sine)
{
    Phasor ph;
    ph.prepare(kSampleRate);
    std::vector<float> x(samples(seconds));
    for (std::size_t n = 0; n < x.size(); ++n) {
        ph.setFrequency(hzAt(n)); // per-sample frequency: cheap enough offline; the phase never jumps
        x[n] = amplitude * sine(ph.tick());
    }
    return x;
}

/// The largest |x[n] − x[n−1]| and where it happens. For a sine of amplitude A at f Hz the largest
/// honest step is ≈ 2π·f/fs·A, so anything far above that is a discontinuity, i.e. a click.
void reportMaxStep(const char* name, const std::vector<float>& x, double boundHz, double amplitude)
{
    float worst = 0.0f;
    std::size_t at = 0;
    for (std::size_t n = 1; n < x.size(); ++n)
        if (const float d = std::abs(x[n] - x[n - 1]); d > worst) {
            worst = d;
            at = n;
        }
    const double bound = 2.0 * std::numbers::pi * boundHz / kSampleRate * amplitude;
    std::printf("  %-22s max step %.4f at sample %zu (t = %.4f s); smooth-sine bound %.4f -> %s\n", name, worst, at,
                static_cast<double>(at) / kSampleRate, bound, worst <= bound * 1.001 ? "no jump" : "JUMP (click)");
}

// ---- D5 experiment ---------------------------------------------------------------------------

struct PrecisionResult {
    double cycles;  // cycles actually completed (wraps + final phase)
};

template <typename T>
PrecisionResult runFloatPhasor(double hz, std::uint64_t n)
{
    // The curriculum's accumulator, in float or double: add, wrap with a subtraction.
    T phase = 0, inc = static_cast<T>(hz / kSampleRate);
    std::uint64_t wraps = 0;
    for (std::uint64_t i = 0; i < n; ++i) {
        phase += inc;
        if (phase >= T(1)) {
            phase -= T(1);
            ++wraps;
        }
    }
    return {static_cast<double>(wraps) + static_cast<double>(phase)};
}

PrecisionResult runFixedPhasor(double hz, std::uint64_t n)
{
    Phasor ph;
    ph.prepare(kSampleRate);
    ph.setFrequency(hz);
    std::uint64_t wraps = 0;
    Phase prev = 0;
    for (std::uint64_t i = 0; i < n; ++i) {
        (void)ph.tick();
        if (ph.phase() < prev) // unsigned wrap detected: the phase went past one cycle
            ++wraps;
        prev = ph.phase();
    }
    return {static_cast<double>(wraps) + dsp::phaseToCycles(ph.phase())};
}

void printPrecision(const char* type, double hz, double exact, PrecisionResult r)
{
    std::printf("  %-7s %8.3f Hz  exact %12.4f cycles  got %14.4f  ", type, hz, exact, r.cycles);
    if (r.cycles <= 0.0 || exact / r.cycles > 2.0) // ran less than half as far as it should: stalled
        std::printf("STALLED (phase stopped advancing)\n");
    else
        std::printf("pitch error %+10.5f cents, phase drift %+.6f cycles\n", 1200.0 * std::log2(r.cycles / exact),
                    r.cycles - exact);
}

void phasePrecisionExperiment()
{
    constexpr std::uint64_t kOneHour = 3600ull * kSampleRate; // 172.8 million samples
    std::printf("\nD5 experiment: phase accumulators after one hour at %d Hz\n", kSampleRate);
    for (double hz : {0.001, 0.1, 440.0}) {
        const double exact = static_cast<double>(kOneHour) * hz / kSampleRate;
        printPrecision("float", hz, exact, runFloatPhasor<float>(hz, kOneHour));
        printPrecision("double", hz, exact, runFloatPhasor<double>(hz, kOneHour));
        printPrecision("uint32", hz, exact, runFixedPhasor(hz, kOneHour));
    }
}

} // namespace

void renderL1_2(const std::filesystem::path& dir)
{
    const auto table = [](Phase p) { return dsp::sineTable(p); };

    // 1. Glides, 110 → 880 Hz (three octaves) in 2 s.
    const std::size_t glideN = samples(2.0);
    const auto t01 = [glideN](std::size_t n) { return static_cast<double>(n) / static_cast<double>(glideN - 1); };

    // Exponential: interpolate the *note* linearly (45 = A2 = 110 Hz → 81 = A5 = 880 Hz), convert to Hz.
    // Equal time per octave, which is how pitch is heard (L0.2).
    const auto glideExp = oscillator(2.0, 0.5f, [&](std::size_t n) {
        return static_cast<double>(util::midiToHz(static_cast<float>(45.0 + 36.0 * t01(n))));
    }, table);
    renderMono(dir / "l1.2_glide_exp.wav", glideExp);

    // Linear in Hz: the first octave (110→220) takes 0.29 s, the last (440→880) takes 1.14 s.
    const auto glideLin = oscillator(2.0, 0.5f, [&](std::size_t n) { return 110.0 + 770.0 * t01(n); }, table);
    renderMono(dir / "l1.2_glide_linhz.wav", glideLin);

    // The fragile formula with a moving f: x = sin(2π·f(t)·t). The real frequency is the derivative of the
    // phase: d/dt[f(t)·t] = f(t) + t·f'(t). For this exponential glide that ends at 880·(1 + ln 8) ≈ 2710 Hz.
    std::vector<float> glideNaive(glideN);
    for (std::size_t n = 0; n < glideN; ++n) {
        const double hz = util::midiToHz(static_cast<float>(45.0 + 36.0 * t01(n)));
        const double t = static_cast<double>(n) / kSampleRate;
        glideNaive[n] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz * t));
    }
    renderMono(dir / "l1.2_glide_naive.wav", glideNaive);

    // 2. A sudden frequency step, 440 → 660 Hz, at 1.0011 s (chosen so the old and new formula phases differ).
    const std::size_t stepAt = samples(1.0011);
    const auto stepHz = [stepAt](std::size_t n) { return n < stepAt ? 440.0 : 660.0; };
    const auto stepPhasor = oscillator(2.0, 0.5f, stepHz, table);
    renderMono(dir / "l1.2_step_phasor.wav", stepPhasor);
    std::vector<float> stepNaive(samples(2.0));
    for (std::size_t n = 0; n < stepNaive.size(); ++n)
        stepNaive[n] = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * stepHz(n) * static_cast<double>(n) / kSampleRate));
    renderMono(dir / "l1.2_step_naive.wav", stepNaive);

    // 3. A chord: three phasors, three sines, just added. 0.3 each keeps the sum below 0.9 even when the
    //    peaks line up.
    {
        const double notes[] = {57.0, 60.0, 64.0}; // A3, C4, E4: A minor
        Phasor ph[3];
        for (int v = 0; v < 3; ++v) {
            ph[v].prepare(kSampleRate);
            ph[v].setFrequency(util::midiToHz(static_cast<float>(notes[v])));
        }
        std::vector<float> chord(samples(2.0));
        for (float& s : chord)
            s = 0.3f * (dsp::sineTable(ph[0].tick()) + dsp::sineTable(ph[1].tick()) + dsp::sineTable(ph[2].tick()));
        renderMono(dir / "l1.2_chord.wav", chord);
    }

    // 4. The three sines at 1 kHz, for spectra.
    const auto at1k = [](std::size_t) { return 1000.0; };
    renderMono(dir / "l1.2_sine1k_table.wav", oscillator(1.0, 0.5f, at1k, table));
    renderMono(dir / "l1.2_sine1k_poly.wav", oscillator(1.0, 0.5f, at1k, [](Phase p) { return dsp::sinePoly(p); }));
    renderMono(dir / "l1.2_sine1k_std.wav", oscillator(1.0, 0.5f, at1k, [](Phase p) { return dsp::sineStd(p); }));

    std::printf("\nDiscontinuity check (largest sample-to-sample step):\n");
    reportMaxStep("glide_exp", glideExp, 880.0, 0.5);
    reportMaxStep("glide_linhz", glideLin, 880.0, 0.5);
    reportMaxStep("step_phasor", stepPhasor, 660.0, 0.5);
    reportMaxStep("step_naive", stepNaive, 660.0, 0.5);

    phasePrecisionExperiment();
}

} // namespace fss::render
