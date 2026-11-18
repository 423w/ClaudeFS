// L5.3 scenarios: the delay line and the tempo-synced delay.
//
//   l5.3_epiano_delay.wav        E-piano phrase through the synced ping-pong delay (1/8 dotted at 120 BPM)
//   l5.3_time_clean.wav          the same phrase; at 2.0 s the delay time changes 375 → 250 ms (Clean: crossfade)
//   l5.3_time_tape.wav           same change in Tape mode: the echoes bend in pitch
//   l5.3_time_jump.wav           same change done the naive way (read position jumps): the click to avoid
//                                Compare: python tools/analyze/click_meter.py <the three> --at 2.0 --channel 0
//   l5.3_tempo_change.wav        sync on, the "host" tempo goes 120 → 90 BPM at 2.0 s (Event::Type::Tempo)
//   l5.3_spill.wav               a chord, the delay switched OFF at 1.0 s: the echoes in flight ring out
//   (console)                    prediction 2: what a fractional linear read does to repeats (dB at fs/4),
//                                and what a delay reset (the FX slot's sleep) costs at 48 and 192 kHz
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/delay_line.h>
#include <fss/fx/delay.h>
#include <fss/synth/params.h>
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

struct At {
    double ms;
    Event e;
};

/// Time-stamped events (notes, params, tempo) through one Engine in 256-sample blocks, stereo.
StereoBuffer renderEvents(const synth::Patch& patch, std::vector<At> events, double totalMs)
{
    std::stable_sort(events.begin(), events.end(), [](const At& a, const At& b) { return a.ms < b.ms; });
    Engine eng;
    eng.prepare(kSampleRate, 256);
    eng.setPatch(patch);
    const auto total = static_cast<std::size_t>(totalMs * 0.001 * kSampleRate);
    StereoBuffer out{std::vector<float>(total), std::vector<float>(total)};
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
        eng.process(block, out.left.data() + start, out.right.data() + start, n);
    }
    return out;
}

/// A short e-piano phrase: four notes, then space for the echoes.
std::vector<At> phrase()
{
    std::vector<At> ev;
    const std::uint8_t keys[] = {64, 67, 71, 74};
    for (int i = 0; i < 4; ++i) {
        const double t = 250.0 * i;
        ev.push_back({t, Event::noteOn(0, keys[i], 0.8f)});
        ev.push_back({t + 180.0, Event::noteOff(0, keys[i])});
    }
    return ev;
}

synth::Patch epianoWithDelay(fx::DelayTimeMode mode, bool sync)
{
    synth::Patch p = synth::presets::epiano();
    p.fx.delay = {.enabled = true, .timeMs = 375.0f, .sync = sync, .division = 6, .feedbackPct = 45.0f, .pingPong = true,
                  .dampingHz = 5000.0f, .mixPct = 35.0f, .timeMode = mode};
    return p;
}

void timeChanges(const std::filesystem::path& out)
{
    writeStereo(out / "l5.3_epiano_delay.wav", renderEvents(epianoWithDelay(fx::DelayTimeMode::Clean, true), phrase(), 5000.0));

    // Change the (unsynced) time at 2.0 s: notes again at 1.8 s, so echoes are sounding through the change.
    auto withChange = [](std::vector<At> ev) {
        for (const At& a : phrase())
            ev.push_back({a.ms + 1800.0, a.e});
        ev.push_back({2000.0, Event::paramChange(0, synth::param_id::kDelayTime, 250.0f)});
        return ev;
    };
    writeStereo(out / "l5.3_time_clean.wav",
                renderEvents(epianoWithDelay(fx::DelayTimeMode::Clean, false), withChange(phrase()), 5000.0));
    writeStereo(out / "l5.3_time_tape.wav",
                renderEvents(epianoWithDelay(fx::DelayTimeMode::Tape, false), withChange(phrase()), 5000.0));

    // The naive way, by hand: the dry e-piano, plus one delay line whose read position jumps at 2.0 s.
    synth::Patch dry = synth::presets::epiano();
    StereoBuffer b = renderEvents(dry, withChange(phrase()), 5000.0);
    dsp::DelayLine<dsp::NoInterp> line;
    line.prepare(48000);
    const fx::MixGains g = fx::equalPowerMix(0.35f);
    for (std::size_t i = 0; i < b.left.size(); ++i) {
        const double d = i < static_cast<std::size_t>(2.0 * kSampleRate) ? 18000.0 : 12000.0;
        const float y = line.read(d - 1.0);
        line.push(b.left[i] + 0.45f * y);
        b.left[i] = g.dry * b.left[i] + g.wet * y;
        b.right[i] = b.left[i];
    }
    writeStereo(out / "l5.3_time_jump.wav", b);
}

void tempoAndSpill(const std::filesystem::path& out)
{
    std::vector<At> ev = phrase();
    for (const At& a : phrase())
        ev.push_back({a.ms + 1800.0, a.e});
    ev.push_back({2000.0, Event::tempo(0, 90.0f)}); // 1/8 D: 375 ms → 500 ms
    writeStereo(out / "l5.3_tempo_change.wav", renderEvents(epianoWithDelay(fx::DelayTimeMode::Clean, true), ev, 6000.0));

    std::vector<At> chord;
    for (std::uint8_t k : {std::uint8_t{60}, std::uint8_t{64}, std::uint8_t{67}}) {
        chord.push_back({0.0, Event::noteOn(0, k, 0.8f)});
        chord.push_back({600.0, Event::noteOff(0, k)});
    }
    chord.push_back({1000.0, Event::paramChange(0, synth::param_id::kDelayOn, 0.0f)});
    chord.push_back({2500.0, Event::noteOn(0, 72, 0.8f)}); // after the switch-off: no echo of this one
    chord.push_back({2800.0, Event::noteOff(0, 72)});
    writeStereo(out / "l5.3_spill.wav", renderEvents(epianoWithDelay(fx::DelayTimeMode::Clean, true), chord, 6000.0));
}

/// Prediction 2: an impulse through a feedback loop of gain 1 at delay 100 + frac, read with linear
/// interpolation. The 10th echo's level at fs/4, relative to the 1st (both measured with a small DFT).
void linearDarkening()
{
    for (double frac : {0.0, 0.25, 0.5}) {
        dsp::DelayLine<dsp::LinearInterp> line;
        line.prepare(4096);
        const double d = 100.0 + frac;
        std::vector<float> y(1200);
        for (std::size_t n = 0; n < y.size(); ++n) {
            const float v = line.read(d - 1.0);
            line.push((n == 0 ? 1.0f : 0.0f) + v);
            y[n] = v;
        }
        auto levelAt = [&](std::size_t echo) { // |DFT at fs/4| over the window around echo k
            std::complex<double> s{};
            const auto c = static_cast<std::size_t>(std::round(static_cast<double>(echo) * d));
            for (std::size_t n = c - 8; n < c + 12; ++n)
                s += static_cast<double>(y[n]) * std::polar(1.0, -std::numbers::pi * 0.5 * static_cast<double>(n));
            return std::abs(s);
        };
        std::printf("linear read, delay %.2f: echo 10 vs echo 1 at fs/4: %+.1f dB (%+.2f dB per pass)\n", d,
                    20.0 * std::log10(levelAt(10) / levelAt(1)), 20.0 * std::log10(levelAt(10) / levelAt(1)) / 9.0);
    }
}

void resetCost()
{
    for (double sr : {48000.0, 192000.0}) {
        fx::StereoDelay d;
        d.prepare(sr, 256);
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 20; ++k)
            d.reset();
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 20.0;
        std::printf("StereoDelay::reset() at %.0f kHz (2 lines of 4 s): %.0f us\n", sr / 1000.0, us);
    }
}

} // namespace

void renderL5_3(const std::filesystem::path& outDir)
{
    timeChanges(outDir);
    tempoAndSpill(outDir);
    linearDarkening();
    resetCost();
}

} // namespace fss::render
