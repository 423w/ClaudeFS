// L2.5 scenarios: playing it like an instrument.
//
//   l2.5_legato_bass.wav          "Legato bass" (mono legato, 80 ms glide) playing a line whose notes overlap:
//                                 one continuous note that slides, envelopes never restart
//   l2.5_mono_bass.wav            the same line in Mono (retrigger) mode: every note re-attacks, still gliding
//   l2.5_poly_bass.wav            the same line in Poly mode, no glide: overlapping notes ring together
//   l2.5_pedal_chords.wav         E-piano chords played short (staccato) with the sustain pedal down per bar
//   l2.5_nopedal_chords.wav       the same keys without the pedal
//   l2.5_bend_{smooth,steps}.wav  a pure sine bent ±2 st by a 7-bit (128-step) wheel at 60 Hz updates, with and
//                                 without bend smoothing: anything above 4 kHz is the staircase
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/biquad.h>
#include <fss/synth/presets.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace fss::render {

namespace {

struct At {
    double ms;
    Event e;
};

/// Render a list of time-stamped events (any kind: notes, pedal, bend) through one Engine, in 256-sample blocks.
std::vector<float> renderEvents(const synth::Patch& patch, std::vector<At> events, double totalMs, bool smoothing = true)
{
    std::stable_sort(events.begin(), events.end(), [](const At& a, const At& b) { return a.ms < b.ms; });
    Engine eng;
    eng.setSmoothingEnabled(smoothing);
    eng.prepare(kSampleRate, 256);
    eng.setPatch(patch);
    const auto total = static_cast<std::size_t>(totalMs * 0.001 * kSampleRate);
    std::vector<float> out(total), r(256);
    std::vector<Event> block;
    std::size_t next = 0;
    for (std::size_t start = 0; start < total; start += 256) {
        const auto n = static_cast<int>(std::min<std::size_t>(256, total - start));
        block.clear();
        while (next < events.size()) {
            const auto at = static_cast<std::size_t>(events[next].ms * 0.001 * kSampleRate);
            if (at >= start + static_cast<std::size_t>(n))
                break;
            Event e = events[next++].e;
            e.frame = static_cast<std::uint32_t>(at - start);
            block.push_back(e);
        }
        eng.process(block, out.data() + start, r.data(), n);
    }
    return out;
}

/// A bass line where each note is held 60 ms into the next (legato phrasing), with one detached gap.
std::vector<At> bassLine()
{
    const std::uint8_t keys[] = {36, 43, 48, 46, 43, 41, 43, 36, 36, 48, 46, 43};
    std::vector<At> ev;
    double t = 0.0;
    for (std::size_t i = 0; i < std::size(keys); ++i) {
        const double len = (i == 7) ? 200.0 : 300.0; // note 8 is detached (released before the next)
        ev.push_back({t, Event::noteOn(0, keys[i], 0.85f)});
        ev.push_back({t + len + (i == 7 ? 0.0 : 60.0), Event::noteOff(0, keys[i])});
        t += 300.0;
    }
    return ev;
}

void writeMono(const std::filesystem::path& p, const std::vector<float>& x)
{
    StereoBuffer b{x, x};
    writeStereo(p, b);
}

/// Energy above 4 kHz: three cascaded high-passes (36 dB/oct), so the ~0.5 kHz sine itself leaks < -100 dB
/// and what's left is the staircase's own distortion. Skips the first 50 ms (the note's attack).
double above4kDb(const std::vector<float>& x)
{
    dsp::BiquadDf1<double> hp[3];
    for (auto& h : hp)
        h.setCoeffs(dsp::designBiquad(dsp::BiquadType::HighPass, kSampleRate, 4000.0, 0.7071));
    double s = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        double y = x[n];
        for (auto& h : hp)
            y = h.tick(y);
        if (n > kSampleRate / 20)
            s += y * y;
    }
    return 10.0 * std::log10(s / static_cast<double>(x.size()) + 1e-30);
}

} // namespace

void renderL2_5(const std::filesystem::path& dir)
{
    // 1. One bass line, three voice modes.
    synth::Patch bass = synth::presets::legatoBass();
    writeMono(dir / "l2.5_legato_bass.wav", renderEvents(bass, bassLine(), 4500.0));
    bass.voice.mode = synth::VoiceMode::Mono;
    writeMono(dir / "l2.5_mono_bass.wav", renderEvents(bass, bassLine(), 4500.0));
    bass.voice = {}; // poly, no glide
    writeMono(dir / "l2.5_poly_bass.wav", renderEvents(bass, bassLine(), 4500.0));

    // 2. Staccato chords, pedal down for each bar (pedal changes just after each new chord, like a pianist).
    const std::uint8_t chords[4][4] = {{57, 60, 64, 67}, {53, 57, 60, 64}, {55, 59, 62, 65}, {52, 56, 59, 64}};
    std::vector<At> pedal, dry;
    for (int bar = 0; bar < 4; ++bar) {
        const double t = bar * 1500.0;
        for (std::uint8_t k : chords[bar]) {
            pedal.push_back({t, Event::noteOn(0, k, 0.7f)});
            pedal.push_back({t + 150.0, Event::noteOff(0, k)}); // keys held only 150 ms
        }
        pedal.push_back({t + 20.0, Event::controller(0, 64, 1.0f)});
        pedal.push_back({t + 1490.0, Event::controller(0, 64, 0.0f)}); // lift just before the next chord
    }
    for (const At& a : pedal)
        if (a.e.type != Event::Type::Controller)
            dry.push_back(a);
    const auto withPedal = renderEvents(synth::presets::epiano(), pedal, 7500.0);
    const auto without = renderEvents(synth::presets::epiano(), dry, 7500.0);
    writeMono(dir / "l2.5_pedal_chords.wav", withPedal);
    writeMono(dir / "l2.5_nopedal_chords.wav", without);
    auto rmsAt = [](const std::vector<float>& x, double ms) {
        const auto a = static_cast<std::size_t>(ms * 0.001 * kSampleRate);
        double s = 0;
        for (std::size_t i = a; i < a + 2400; ++i)
            s += static_cast<double>(x[i]) * x[i];
        return 10.0 * std::log10(s / 2400.0 + 1e-30);
    };
    std::printf("  chords, level 1.0 s into the bar (dB): with pedal %.1f, without %.1f\n", rmsAt(withPedal, 1000.0),
                rmsAt(without, 1000.0));

    // 3. Pitch bend from a 7-bit wheel: 128 steps, a new value every 1/60 s, swept -1 -> +1 -> -1 over 4 s.
    synth::Patch sine;
    sine.algorithm = synth::kAlgorithms[7];
    sine.algorithmNumber = 8;
    sine.algorithm.carrier = {1.0f, 0.0f, 0.0f, 0.0f};
    sine.ops[0] = {.level = 0.5f, .env = {.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 100.0f}};
    std::vector<At> bends{{0.0, Event::noteOn(0, 69, 1.0f)}};
    for (int f = 0; f < 240; ++f) {
        const double x = f / 240.0, tri = 1.0 - std::abs(2.0 * x - 1.0);
        const double v = std::round((tri * 2.0 - 1.0) * 63.5) / 63.5; // 7 bits
        bends.push_back({f * 1000.0 / 60.0, Event::pitchBend(0, static_cast<float>(v))});
    }
    const auto smooth = renderEvents(sine, bends, 4200.0, true), steps = renderEvents(sine, bends, 4200.0, false);
    writeMono(dir / "l2.5_bend_smooth.wav", smooth);
    writeMono(dir / "l2.5_bend_steps.wav", steps);
    std::printf("  7-bit bend staircase on a 440 Hz sine, energy above 4 kHz: smoothed %.1f dB, unsmoothed %.1f dB\n",
                above4kDb(smooth), above4kDb(steps));
}

} // namespace fss::render
