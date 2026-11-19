// L4.2 scenarios: operator waveforms, FM aliasing across the keyboard, 1× vs 2× FM core, the new presets.
//
//   (console)  prediction 1: each waveform as a carrier: harmonics 1-8 (dB re the strongest), and its alias power
//              at 2.6 kHz (sine variants naive, saw/square/triangle band-limited)
//   (console)  prediction 2: each waveform as a MODULATOR at index 1 (sine carrier, 1:1): partials within -40 dB
//   l4.2_wave_<name>.wav                     each waveform as a carrier, 220 Hz, 1 s (listen / plot_spectrum.py)
//   (console)  prediction 3: FM aliasing across the keyboard, a bright 2-op patch, FM core at 1x and at 2x
//   (console)  prediction 5: a saw carrier under phase modulation, band-limited vs naive
//   l4.2_<preset>.wav                        the new factory presets (Saw lead, Supersaw, Wave bass, Wave clav)
//   (console)  supersaw mono check; CPU of the waveforms and of the 2x core
#include "alias_meter.h"
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/halfband.h>
#include <fss/dsp/op_waves.h>
#include <fss/synth/fm_voice.h>
#include <fss/synth/operator.h>
#include <fss/synth/presets.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

namespace fss::render {

namespace {

constexpr double kFs = kSampleRate;
constexpr const char* kWaveNames[] = {"Sine", "Half sine", "Abs sine", "Quarter sine", "Alt sine",
                                      "Camel", "Saw", "Square", "Triangle"};
constexpr const char* kWaveFiles[] = {"sine", "half", "abs", "quarter", "alt", "camel", "saw", "square", "triangle"};

/// A carrier with waveform `w`, optionally modulated by an operator with waveform `mw` at index `index`, both at
/// f0 = k·fs/n. Returns n samples after 0.1 s of settling.
std::vector<float> tone(dsp::OpWave w, std::size_t n, std::size_t k, float index = 0.0f, dsp::OpWave mw = dsp::OpWave::Sine)
{
    synth::Operator car, mod;
    for (synth::Operator* o : {&car, &mod}) {
        o->prepare(kFs, 0.0f);
        o->setFrequency(static_cast<double>(k) * kFs / static_cast<double>(n));
        o->setEnvelope({.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 1.0f});
        o->noteOn();
    }
    car.setLevel(1.0f);
    car.setWave(w);
    mod.setLevel(index);
    mod.setWave(mw);
    std::vector<float> y;
    for (std::size_t i = 0; i < n + 4800; ++i) {
        const float v = car.tick(mod.tick());
        if (i >= 4800)
            y.push_back(v);
    }
    return y;
}

/// |DFT bin b| of x (direct sum), as an amplitude.
double bin(const std::vector<float>& x, std::size_t b)
{
    std::complex<double> s = 0.0;
    const double n = static_cast<double>(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        s += static_cast<double>(x[i]) * std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(b * i % x.size()) / n);
    return 2.0 * std::abs(s) / n;
}

void carriers(const std::filesystem::path& out)
{
    constexpr std::size_t kN = 8192, kK = 33; // 193 Hz: plenty of harmonics below Nyquist, aliasing negligible
    constexpr std::size_t kNa = 65536, kKa = 3549; // 2599 Hz: where aliasing shows
    std::printf("prediction 1: waveforms as carriers. Harmonics 1..8 in dB re the strongest; alias power at 2.6 kHz\n");
    for (int w = 0; w < dsp::kNumOpWaves; ++w) {
        const auto x = tone(static_cast<dsp::OpWave>(w), kN, kK);
        double h[9]{}, top = 0.0;
        for (int i = 1; i <= 8; ++i)
            top = std::max(top, h[i] = bin(x, kK * static_cast<std::size_t>(i)));
        std::printf("  %-12s", kWaveNames[w]);
        for (int i = 1; i <= 8; ++i)
            std::printf(" %6.1f", h[i] > 1e-6 * top ? 20.0 * std::log10(h[i] / top) : -999.0);
        const auto a = tone(static_cast<dsp::OpWave>(w), kNa, kKa);
        std::printf("   alias %6.1f dB\n", measureAliasing(a, kKa).aliasDb());
        char file[64];
        std::snprintf(file, sizeof file, "l4.2_wave_%s.wav", kWaveFiles[w]);
        synth::Operator o;
        o.prepare(kFs, 0.0f);
        o.setFrequency(220.0);
        o.setLevel(0.5f);
        o.setWave(static_cast<dsp::OpWave>(w));
        o.setEnvelope({.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 50.0f});
        o.noteOn();
        std::vector<float> y(static_cast<std::size_t>(kFs));
        for (std::size_t i = 0; i < y.size(); ++i) {
            if (i == y.size() - 4800)
                o.noteOff();
            y[i] = o.tick();
        }
        renderMono(out / file, y);
    }
}

void modulators()
{
    constexpr std::size_t kN = 8192, kK = 33;
    std::printf("prediction 2: each waveform as a modulator at index 1 (sine carrier, 1:1): partials within -40 dB\n ");
    for (int w = 0; w < dsp::kNumOpWaves; ++w) {
        const auto x = tone(dsp::OpWave::Sine, kN, kK, 1.0f, static_cast<dsp::OpWave>(w));
        std::vector<double> h;
        double top = 0.0;
        for (std::size_t i = 0; i * kK < kN / 2; ++i) {
            h.push_back(bin(x, i * kK));
            top = std::max(top, h.back());
        }
        int count = 0;
        for (std::size_t i = 1; i < h.size(); ++i)
            count += h[i] > 0.01 * top ? 1 : 0;
        std::printf(" %s %d,", kWaveNames[w], count);
    }
    std::printf("\n");
}

/// The bright 2-op patch of prediction 3: A (index 4, feedback 0.25) > B (carrier), both at exactly f0.
struct BrightFm {
    synth::Patch patch;
    synth::Algorithm alg{};
    explicit BrightFm(double f0)
    {
        for (int i = 0; i < 2; ++i)
            patch.ops[static_cast<std::size_t>(i)] = {.fixed = true, .fixedHz = util::Hz{f0}, .level = i == 0 ? 4.0f : 1.0f,
                                                      .env = {.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 1.0f}};
        patch.ops[2].level = patch.ops[3].level = 0.0f;
        alg.mod[0][1] = 1.0f;
        // Feedback acts on the operator OUTPUT, level 4 included: 0.25 × 4 = an effective feedback index of 1, the
        // saw-like regime. (My first tries, 1.5 and 0.7, are indices 6 and 2.8: chaotic, noise, not a periodic tone.)
        alg.mod[0][0] = 0.25f;
        alg.carrier[1] = 1.0f;
    }
};

/// The FM core at 1x, or at 2x with a 63-tap halfband down to the base rate (and one 2x-rate sample of padding,
/// so the delay is whole: dsp/halfband.h). n samples after 0.1 s.
std::vector<float> fmCore(double f0, int factor, std::size_t n)
{
    BrightFm b(f0);
    synth::FmVoice v;
    v.prepare(kFs * factor, 0.0f);
    v.start({.key = 60, .velocity = 1.0f}, b.patch);
    dsp::Halfband<63> down;
    down.design(9.6);
    std::vector<float> y;
    for (std::size_t i = 0; i < n + 4800; ++i) {
        float s = 0.0f;
        if (factor == 1)
            s = v.tick(b.alg);
        else {
            const float e = v.tick(b.alg), o = v.tick(b.alg);
            s = down.down(e, o);
        }
        if (i >= 4800)
            y.push_back(s);
    }
    return y;
}

void keyboard()
{
    constexpr std::size_t kN = 65536;
    std::printf("prediction 3: FM aliasing across the keyboard (A index 4 + feedback 0.25 > B), re the whole signal\n");
    std::printf("  key   f0 Hz     1x core          2x core        [below 16 kHz: 1x / 2x]\n");
    const std::size_t low = static_cast<std::size_t>(16000.0 * kN / kFs);
    for (int key : {36, 48, 60, 72, 84, 96, 108}) {
        const double want = 440.0 * std::pow(2.0, (key - 69) / 12.0);
        std::size_t k = static_cast<std::size_t>(std::lround(want * kN / kFs)) | 1u; // odd: alias_meter.h
        const double f0 = static_cast<double>(k) * kFs / static_cast<double>(kN);
        const AliasMeasurement a1 = measureAliasing(fmCore(f0, 1, kN), k, low);
        const AliasMeasurement a2 = measureAliasing(fmCore(f0, 2, kN), k, low);
        std::printf("  %3d %8.1f   %8.1f dB      %8.1f dB      [%6.1f / %6.1f]\n", key, f0, a1.aliasDb(), a2.aliasDb(),
                    a1.lowAliasDb(), a2.lowAliasDb());
    }
}

void modulatedSaw()
{
    constexpr std::size_t kN = 65536, kK = 3549;
    std::printf("prediction 5: saw carrier under PM (sine modulator 1:1), alias power: band-limited vs naive\n");
    for (float index : {0.0f, 0.5f, 1.0f, 3.0f}) {
        const auto blep = tone(dsp::OpWave::Saw, kN, kK, index);
        dsp::Phasor pc, pm;
        pc.prepare(kFs);
        pm.prepare(kFs);
        pc.setFrequency(static_cast<double>(kK) * kFs / static_cast<double>(kN));
        pm.setFrequency(static_cast<double>(kK) * kFs / static_cast<double>(kN));
        std::vector<float> naive;
        for (std::size_t i = 0; i < kN + 4800; ++i) {
            const float m = index * dsp::sineTable(pm.tick());
            const float v = dsp::opWave(dsp::OpWave::Saw, pc.tick() + synth::radiansToPhase(m), 0.0);
            if (i >= 4800)
                naive.push_back(v);
        }
        std::printf("  index %.1f: band-limited %6.1f dB, naive %6.1f dB\n", index, measureAliasing(blep, kK).aliasDb(),
                    measureAliasing(naive, kK).aliasDb());
    }
}

void presets(const std::filesystem::path& out)
{
    std::vector<ScoreNote> chords, line, bass;
    const std::uint8_t v[3][4] = {{57, 60, 64, 69}, {53, 57, 60, 65}, {55, 59, 62, 67}};
    for (int b = 0; b < 3; ++b)
        for (std::uint8_t k : v[b])
            chords.push_back({.startMs = b * 1500.0, .key = k, .velocity = 100, .durationMs = 1300.0});
    const std::uint8_t mel[] = {69, 72, 74, 76, 74, 72, 69, 67};
    for (int i = 0; i < 8; ++i)
        line.push_back({.startMs = i * 250.0, .key = mel[i], .velocity = 100, .durationMs = 240.0});
    const std::uint8_t bl[] = {33, 33, 45, 33, 36, 36, 48, 38};
    for (int i = 0; i < 8; ++i)
        bass.push_back({.startMs = i * 250.0, .key = bl[i], .velocity = static_cast<std::uint8_t>(80 + 5 * (i % 4)), .durationMs = 180.0});
    writeStereo(out / "l4.2_saw_lead.wav", renderWithEngine(synth::presets::sawLead(), line, {.tailMs = 1500.0}));
    const StereoBuffer ss = renderWithEngine(synth::presets::supersaw(), chords, {.tailMs = 2500.0});
    writeStereo(out / "l4.2_supersaw.wav", ss);
    writeStereo(out / "l4.2_wave_bass.wav", renderWithEngine(synth::presets::waveBass(), bass, {.tailMs = 800.0}));
    writeStereo(out / "l4.2_wave_clav.wav", renderWithEngine(synth::presets::waveClav(), line, {.tailMs = 800.0}));
    double m = 0.0, s = 0.0, l = 0.0;
    for (std::size_t i = 0; i < ss.left.size(); ++i) {
        const double mid = 0.5 * (ss.left[i] + ss.right[i]), side = 0.5 * (ss.left[i] - ss.right[i]);
        m += mid * mid;
        s += side * side;
        l += 0.5 * (static_cast<double>(ss.left[i]) * ss.left[i] + static_cast<double>(ss.right[i]) * ss.right[i]);
    }
    std::printf("supersaw: mono sum %.1f dB vs the stereo channels' average; side/mid %.1f dB\n", 10.0 * std::log10(m / l),
                10.0 * std::log10(s / m));
}

void cost()
{
    auto time = [](auto&& f) {
        const auto t0 = std::chrono::steady_clock::now();
        float acc = 0.0f;
        for (int i = 0; i < 480000; ++i)
            acc += f();
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 480000.0;
        return acc == 12345.0f ? -1.0 : ns; // keep the work alive
    };
    std::printf("CPU per operator-sample:");
    for (int w : {0, 1, 3, 6, 8}) {
        synth::Operator o;
        o.prepare(kFs, 0.0f);
        o.setFrequency(440.0);
        o.setLevel(1.0f);
        o.setWave(static_cast<dsp::OpWave>(w));
        o.setEnvelope({.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 1.0f});
        o.noteOn();
        std::printf("  %s %.1f ns", kWaveNames[w], time([&] { return o.tick(0.1f); }));
    }
    std::printf("\n");
    for (int factor : {1, 2}) {
        BrightFm b(440.0);
        synth::FmVoice v;
        v.prepare(kFs * factor, 0.0f);
        v.start({.key = 60, .velocity = 1.0f}, b.patch);
        dsp::Halfband<63> down;
        down.design(9.6);
        const double ns = factor == 1 ? time([&] { return v.tick(b.alg); })
                                      : time([&] { const float e = v.tick(b.alg); return down.down(e, v.tick(b.alg)); });
        std::printf("CPU FM core (4 ops) at %dx: %.1f ns per output sample\n", factor, ns);
    }
}

} // namespace

void renderL4_2(const std::filesystem::path& outDir)
{
    carriers(outDir);
    modulators();
    keyboard();
    modulatedSaw();
    cost();
    presets(outDir);
}

} // namespace fss::render
