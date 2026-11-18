// L5.1 scenarios: the FX chain's switching, and the D16 measurement.
//
//   l5.1_width_toggle.wav    a stereo test tone (L 220 Hz, R 330 Hz) through the width slot: switched on at
//                            width 0 % (mono) at 1.0 s, off at 2.0 s, on again at 3.0 s, then swept
//                            0 → 200 % from 3.5 s to 5.5 s. Every switch is the slot's 10 ms fade.
//   l5.1_width_hard.wav      the same switches done instantly (no fade): the clicks the slot exists to avoid.
//                            Compare: python tools/analyze/click_meter.py <file> --at 1 --at 2 --at 3
//   l5.1_gain_toggle.wav     the e-piano playing, with a -12 dB Gain slot switched on and off every 0.5 s.
//   (console)                D16: the same three-effect chain called through a tuple (static dispatch, what
//                            CFS uses) and through virtual calls on a base class, ns per sample.
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/fx/effect.h>
#include <fss/fx/fx_chain.h>
#include <fss/fx/gain.h>
#include <fss/fx/width.h>
#include <fss/synth/presets.h>
#include <fss/util/math.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <tuple>
#include <vector>

namespace fss::render {

namespace {

constexpr int kBlock = 256;

StereoBuffer testTone(double seconds)
{
    const auto n = static_cast<std::size_t>(seconds * kSampleRate);
    StereoBuffer b{std::vector<float>(n), std::vector<float>(n)};
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        b.left[i] = 0.4f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * t));
        b.right[i] = 0.4f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 330.0 * t));
    }
    return b;
}

void widthToggle(const std::filesystem::path& out)
{
    StereoBuffer b = testTone(6.0);
    fx::FxSlot<fx::StereoWidth> slot;
    slot.prepare(kSampleRate, kBlock);
    slot.effect().setParams({.enabled = true, .widthPct = 0.0f});
    slot.reset();
    const auto at = [](double s) { return static_cast<std::size_t>(s * kSampleRate); };
    for (std::size_t s = 0; s < b.left.size(); s += kBlock) {
        if (s == at(1.0) || s == at(3.0))
            slot.setEnabled(true);
        if (s == at(2.0))
            slot.setEnabled(false);
        if (s >= at(3.5) && s < at(5.5)) // the sweep: a new target every block, smoothed inside the effect
            slot.effect().setParams({.enabled = true, .widthPct = 200.0f * static_cast<float>(s - at(3.5)) / static_cast<float>(at(2.0))});
        const auto n = static_cast<int>(std::min<std::size_t>(kBlock, b.left.size() - s));
        slot.process(b.left.data() + s, b.right.data() + s, n);
    }
    writeStereo(out / "l5.1_width_toggle.wav", b);

    // The same switches without the slot: width 0 % applied or not, instantly.
    StereoBuffer h = testTone(6.0);
    for (std::size_t i = 0; i < h.left.size(); ++i) {
        const bool on = (i >= at(1.0) && i < at(2.0)) || (i >= at(3.0) && i < at(3.5));
        if (on) {
            const float m = 0.5f * (h.left[i] + h.right[i]);
            h.left[i] = h.right[i] = m;
        }
    }
    writeStereo(out / "l5.1_width_hard.wav", h);
}

void gainToggle(const std::filesystem::path& out)
{
    // E-piano chords through the Engine, then a Gain slot on the result, toggled every 0.5 s.
    std::vector<ScoreNote> score;
    const std::uint8_t chord[] = {60, 64, 67, 71};
    for (int bar = 0; bar < 4; ++bar)
        for (std::uint8_t k : chord)
            score.push_back({.startMs = bar * 1000.0, .key = static_cast<std::uint8_t>(k + bar * 2), .velocity = 100, .durationMs = 900.0});
    StereoBuffer b = renderWithEngine(synth::presets::epiano(), score, {.tailMs = 500.0});
    fx::FxSlot<fx::Gain> slot;
    slot.prepare(kSampleRate, kBlock);
    slot.effect().snapDb(-12.0f);
    for (std::size_t s = 0; s < b.left.size(); s += kBlock) {
        slot.setEnabled((s / static_cast<std::size_t>(kSampleRate / 2)) % 2 == 1);
        const auto n = static_cast<int>(std::min<std::size_t>(kBlock, b.left.size() - s));
        slot.process(b.left.data() + s, b.right.data() + s, n);
    }
    writeStereo(out / "l5.1_gain_toggle.wav", b);
}

// ---- D16: static vs virtual dispatch -----------------------------------------------------------------

struct VirtualEffect {
    virtual ~VirtualEffect() = default;
    virtual void process(float* L, float* R, int n) noexcept = 0;
};
template <class Slot>
struct VirtualWrap final : VirtualEffect {
    Slot slot;
    void process(float* L, float* R, int n) noexcept override { slot.process(L, R, n); }
};

template <class F>
double nsPerSample(F&& processBlock, int block)
{
    std::vector<float> L(static_cast<std::size_t>(block), 0.1f), R(static_cast<std::size_t>(block), -0.1f);
    const int blocks = 48000 * 20 / block; // 20 s of audio
    for (int k = 0; k < blocks / 10; ++k) // warm up
        processBlock(L.data(), R.data(), block);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < blocks; ++k)
        processBlock(L.data(), R.data(), block);
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    return ns / (static_cast<double>(blocks) * block);
}

void dispatchBenchmark()
{
    using W = fx::FxSlot<fx::StereoWidth>;
    using G = fx::FxSlot<fx::Gain>;
    for (int block : {16, 64, 256}) {
        // Static: a tuple, visited with a fold expression (what FxChain does).
        std::tuple<W, G, W> t;
        std::apply([&](auto&... s) { (s.prepare(kSampleRate, block), ...); }, t);
        std::apply([&](auto&... s) { (s.setEnabled(true), ...); }, t);
        std::apply([&](auto&... s) { (s.reset(), ...); }, t);
        const double staticNs = nsPerSample(
            [&](float* L, float* R, int n) { std::apply([&](auto&... s) { (s.process(L, R, n), ...); }, t); }, block);

        // Virtual: the same three slots behind base-class pointers (what a reorderable chain would hold).
        std::vector<std::unique_ptr<VirtualEffect>> v;
        auto add = [&](auto wrap) {
            wrap->slot.prepare(kSampleRate, block);
            wrap->slot.setEnabled(true);
            wrap->slot.reset();
            v.push_back(std::move(wrap));
        };
        add(std::make_unique<VirtualWrap<W>>());
        add(std::make_unique<VirtualWrap<G>>());
        add(std::make_unique<VirtualWrap<W>>());
        const double virtualNs = nsPerSample(
            [&](float* L, float* R, int n) {
                for (auto& e : v)
                    e->process(L, R, n);
            },
            block);
        std::printf("D16 dispatch, block %3d: tuple %.3f ns/sample, virtual %.3f ns/sample (%+.1f %%)\n", block,
                    staticNs, virtualNs, 100.0 * (virtualNs - staticNs) / staticNs);
    }
}

} // namespace

void renderL5_1(const std::filesystem::path& outDir)
{
    widthToggle(outDir);
    gainToggle(outDir);
    dispatchBenchmark();
}

} // namespace fss::render
