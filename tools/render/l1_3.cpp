// L1.3 scenarios: the ADSR envelope.
//
//   l1.3_env_{pluck,pad,organ}.wav   the envelope alone, as a 0..1 "DC" signal: plot it with plot_wave.py
//   l1.3_{pluck,pad,organ}.wav       the envelope on a 440 Hz sine that starts at its PEAK (worst case for
//                                    clicks: without the envelope the first sample would jump 0 → 0.5)
//   l1.3_gate_hard.wav               the same sine gated on/off with no envelope: the click reference
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/adsr.h>
#include <fss/dsp/phasor.h>
#include <fss/dsp/sine.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fss::render {

namespace {

struct Patch {
    const char* name;
    dsp::AdsrParams env;
    double gateSeconds; // key held this long
    double totalSeconds;
};

// The three patches from the curriculum. Decay/sustain for pad and organ are my additions where it gave none.
constexpr Patch kPatches[] = {
    {"pluck", {.attackMs = 1.0f, .decayMs = 300.0f, .sustain = 0.0f, .releaseMs = 300.0f}, 1.0, 1.5},
    {"pad", {.attackMs = 800.0f, .decayMs = 600.0f, .sustain = 0.8f, .releaseMs = 1500.0f}, 2.0, 4.0},
    {"organ", {.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 10.0f}, 1.0, 1.25},
};

void renderPatch(const std::filesystem::path& dir, const Patch& p)
{
    dsp::Adsr env;
    env.prepare(kSampleRate);
    env.setParams(p.env);
    dsp::Phasor osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0);
    osc.reset(dsp::phaseFromCycles(0.25)); // start at the sine's peak on purpose

    const auto n = static_cast<std::size_t>(std::lround(p.totalSeconds * kSampleRate));
    const auto gateOff = static_cast<std::size_t>(std::lround(p.gateSeconds * kSampleRate));
    std::vector<float> envOut(n), audio(n);
    env.noteOn();
    for (std::size_t i = 0; i < n; ++i) {
        if (i == gateOff)
            env.noteOff();
        envOut[i] = env.tick();
        audio[i] = 0.5f * envOut[i] * dsp::sineTable(osc.tick());
    }
    renderMono(dir / ("l1.3_env_" + std::string(p.name) + ".wav"), envOut);
    renderMono(dir / ("l1.3_" + std::string(p.name) + ".wav"), audio);
}

} // namespace

void renderL1_3(const std::filesystem::path& dir)
{
    for (const Patch& p : kPatches)
        renderPatch(dir, p);

    // Reference: hard gate, no envelope. Starts at the peak and stops mid-cycle (1.0011 s), like L1.1's click demo.
    dsp::Phasor osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(440.0);
    osc.reset(dsp::phaseFromCycles(0.25));
    std::vector<float> hard(static_cast<std::size_t>(std::lround(1.0011 * kSampleRate)));
    for (float& s : hard)
        s = 0.5f * dsp::sineTable(osc.tick());
    renderMono(dir / "l1.3_gate_hard.wav", hard);

    // Timing check across sample rates: release 500 ms from full scale should reach −60 dB at 500 ms.
    std::printf("\nRelease timing (releaseMs = 500, from full scale):\n");
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        dsp::Adsr e;
        e.prepare(fs);
        e.setParams({.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 500.0f});
        e.noteOn();
        while (e.stage() != dsp::Adsr::Stage::Sustain)
            (void)e.tick();
        e.noteOff();
        long n60 = 0, n0 = 0;
        for (long i = 1; e.isActive(); ++i) {
            const float v = e.tick();
            if (n60 == 0 && v <= 0.001f)
                n60 = i;
            n0 = i;
        }
        std::printf("  %6.0f Hz: -60 dB after %7.2f ms, silent (idle) after %7.2f ms\n", fs, 1000.0 * n60 / fs,
                    1000.0 * n0 / fs);
    }
}

} // namespace fss::render
