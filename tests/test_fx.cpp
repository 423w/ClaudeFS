// Tests for the FX layer (L5.1): the effect contract, the slot's click-free switching and tail handling,
// the two trivial effects (width, gain), and block-size invariance of all of it.
#include <fss/engine.h>
#include <fss/fx/effect.h>
#include <fss/fx/fx_chain.h>
#include <fss/fx/gain.h>
#include <fss/fx/width.h>
#include <fss/synth/params.h>
#include <fss/util/math.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

/// A test-only effect with a tail: one echo line, 100 samples, feedback 0.5, output = input + echoes.
/// Spills its tail, like the real delay (L5.3) will.
struct TestEcho {
    static constexpr bool kSpillsTail = true;
    static constexpr int kDelay = 100;
    std::vector<float> bufL, bufR;
    int pos = 0;
    void prepare(double, int)
    {
        bufL.assign(kDelay, 0.0f);
        bufR.assign(kDelay, 0.0f);
    }
    void reset() noexcept
    {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        pos = 0;
    }
    [[nodiscard]] int tailSamples() const noexcept { return kDelay * 20; } // 0.5^20: 120 dB down
    void process(fx::StereoBlock b) noexcept
    {
        for (int i = 0; i < b.n; ++i) {
            const auto p = static_cast<std::size_t>(pos);
            const float dl = bufL[p], dr = bufR[p];
            bufL[p] = b.L[i] + 0.5f * dl;
            bufR[p] = b.R[i] + 0.5f * dr;
            b.L[i] += dl;
            b.R[i] += dr;
            pos = (pos + 1) % kDelay;
        }
    }
};
static_assert(fx::StereoEffect<TestEcho>);

/// Something that is not an effect: process() isn't noexcept. The concept must say no.
struct NotAnEffect {
    static constexpr bool kSpillsTail = false;
    void prepare(double, int) {}
    void reset() noexcept {}
    void process(fx::StereoBlock) {}
    [[nodiscard]] int tailSamples() const noexcept { return 0; }
};
static_assert(!fx::StereoEffect<NotAnEffect>);

float maxStep(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float m = 0.0f;
    for (std::size_t i = from + 1; i < to; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

} // namespace

TEST_CASE("fx mix laws: linear sums to 1, equal power keeps power, both exact at the ends", "[fx]")
{
    for (float m = 0.0f; m <= 1.0f; m += 0.125f) {
        const fx::MixGains lin = fx::linearMix(m);
        const fx::MixGains ep = fx::equalPowerMix(m);
        CHECK(std::abs(lin.dry + lin.wet - 1.0f) < 1e-6f);
        CHECK(std::abs(ep.dry * ep.dry + ep.wet * ep.wet - 1.0f) < 1e-6f);
    }
    CHECK(fx::equalPowerMix(0.0f).wet == 0.0f);
    CHECK(fx::equalPowerMix(0.0f).dry == 1.0f);
    CHECK(fx::equalPowerMix(1.0f).dry == 0.0f);
    CHECK(fx::equalPowerMix(1.0f).wet == 1.0f);
    CHECK(fx::equalPowerMix(0.5f).wet > 0.70f); // -3 dB each in the middle, where linear gives -6 dB
}

TEST_CASE("width: 0 % is mono, 100 % changes nothing, 200 % doubles the side", "[fx]")
{
    fx::StereoWidth w;
    w.prepare(kFs, 64);
    auto run = [&](float pct, float l, float r) {
        w.setParams({.enabled = true, .widthPct = pct});
        w.reset(); // snap the smoother: we test the static law here
        float L = l, R = r;
        w.process({&L, &R, 1});
        return std::pair{L, R};
    };
    const auto mono = run(0.0f, 0.8f, 0.2f);
    CHECK(mono.first == mono.second);
    CHECK(std::abs(mono.first - 0.5f) < 1e-6f);
    const auto same = run(100.0f, 0.8f, 0.2f);
    CHECK(std::abs(same.first - 0.8f) < 1e-6f);
    CHECK(std::abs(same.second - 0.2f) < 1e-6f);
    const auto wide = run(200.0f, 0.8f, 0.2f); // M 0.5, S 0.3 → 0.6
    CHECK(std::abs(wide.first - 1.1f) < 1e-6f);
    CHECK(std::abs(wide.second + 0.1f) < 1e-6f);
    const auto monoIn = run(200.0f, 0.4f, 0.4f); // a mono input stays mono, however wide
    CHECK(monoIn.first == monoIn.second);
}

TEST_CASE("FxSlot: switching fades over 10 ms (no click), and off is a bit-exact bypass", "[fx]")
{
    fx::FxSlot<fx::Gain> slot;
    slot.prepare(kFs, 256);
    slot.effect().snapDb(-6.0206f); // x 0.5
    const int fade = static_cast<int>(fx::kSlotFadeMs * 0.001 * kFs); // 480 samples
    const std::size_t total = 4096;
    std::vector<float> L(total, 1.0f), R(total, -1.0f);

    // Off and asleep: untouched, bit for bit.
    slot.process(L.data(), R.data(), 256);
    CHECK(std::all_of(L.begin(), L.begin() + 256, [](float v) { return v == 1.0f; }));

    // Switch on at sample 256: a straight ramp from 1 to 0.5 over 480 samples.
    slot.setEnabled(true);
    for (std::size_t s = 256; s < 2048; s += 256)
        slot.process(L.data() + s, R.data() + s, 256);
    CHECK(L[256] < 1.0f);
    CHECK(L[256] > 0.99f);                      // the first step is one 480th of the way
    CHECK(std::abs(L[256 + fade + 10] - 0.5f) < 1e-6f);
    CHECK(maxStep(L, 250, 2048) < 0.5f / static_cast<float>(fade) + 1e-6f); // no step bigger than the ramp's
    CHECK(std::abs(R[256 + fade + 10] + 0.5f) < 1e-6f);

    // Switch off at 2048: back up to 1, then exactly the input again.
    slot.setEnabled(false);
    for (std::size_t s = 2048; s < total; s += 256)
        slot.process(L.data() + s, R.data() + s, 256);
    CHECK(maxStep(L, 2040, total) < 0.5f / static_cast<float>(fade) + 1e-6f);
    CHECK(L[2048 + static_cast<std::size_t>(fade) + 1] == 1.0f);
    CHECK(L[total - 1] == 1.0f);
    CHECK_FALSE(slot.awake()); // and the effect is asleep again (costs nothing)
}

TEST_CASE("FxSlot: switching a spilling effect off lets its tail ring out, then it sleeps", "[fx]")
{
    fx::FxSlot<TestEcho> slot;
    slot.prepare(kFs, 512);
    slot.setEnabled(true);
    slot.reset(); // start switched on, no fade
    std::vector<float> L(20000, 0.0f), R(20000, 0.0f);
    L[0] = 1.0f; // one click in
    slot.process(L.data(), R.data(), 512);
    CHECK(L[0] == 1.0f);
    CHECK(L[100] == 1.0f); // first echo
    CHECK(L[200] == 0.5f); // second
    // Switch off while echoes are in flight: they keep coming (not cut), and new input passes dry.
    slot.setEnabled(false);
    L[600] = 0.25f; // played after the switch-off (past the 10 ms fade? no: inside it, so partly fed)
    L[5000] = 0.25f; // well after the fade: must pass dry and make no echo
    for (std::size_t s = 512; s < L.size(); s += 512)
        slot.process(L.data() + s, R.data() + s, static_cast<int>(std::min<std::size_t>(512, L.size() - s)));
    CHECK(L[700] != 0.0f);        // echoes still sounding after the switch-off (here: the 7th, 1/64)
    CHECK(L[5000] == 0.25f);      // dry
    CHECK(L[5100] == 0.0f);       // and no echo of it: the effect isn't fed any more
    CHECK_FALSE(slot.awake());    // 2000 silent samples after the last echo: asleep
    CHECK(slot.tailSamples() == 0); // off and asleep: no tail to report
}

TEST_CASE("FxSlot: an effect with a tail sleeps after silence and wakes on the first sample", "[fx]")
{
    fx::FxSlot<TestEcho> slot;
    slot.prepare(kFs, 1024);
    slot.setEnabled(true);
    slot.reset();
    std::vector<float> L(8192, 0.0f), R(8192, 0.0f);
    L[10] = 1.0f;
    L[6000] = 1.0f; // after ~5990 samples of decay: well past the 2000-sample tail
    std::vector<bool> awake;
    for (std::size_t s = 0; s < L.size(); s += 1024) {
        slot.process(L.data() + s, R.data() + s, 1024);
        awake.push_back(slot.awake());
    }
    CHECK(awake[0]);
    CHECK_FALSE(awake[4]); // samples 4096..5119: the echoes of sample 10 died out, the effect sleeps
    CHECK(awake[5]);       // 5120..6143: woken by the click at 6000
    CHECK(L[6000] == 1.0f);
    CHECK(L[6100] == 1.0f); // and it echoes again, from a clean state
}

TEST_CASE("FxSlot and FxChain: block-size invariant with switches and parameter changes", "[fx][invariance]")
{
    // A random stereo signal with silent gaps, the echo and a width slot switched on and off at random
    // samples, a width sweep. Rendered in blocks of 1, 7, 64 and 1000: bit-identical.
    constexpr std::size_t kLen = 30000;
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> sig(-1.0f, 1.0f);
    std::vector<float> inL(kLen), inR(kLen);
    for (std::size_t i = 0; i < kLen; ++i) {
        const bool gap = (i / 3000) % 3 == 2; // every third 3000-sample stretch is silence
        inL[i] = gap ? 0.0f : sig(rng);
        inR[i] = gap ? 0.0f : sig(rng);
    }
    struct Change {
        std::size_t at;
        int what; // 0 echo on, 1 echo off, 2 width on, 3 width off, 4 width %
        float value;
    };
    std::vector<Change> changes;
    std::uniform_int_distribution<std::size_t> when(0, kLen - 1);
    std::uniform_int_distribution<int> what(0, 4);
    for (int k = 0; k < 40; ++k)
        changes.push_back({when(rng), what(rng), sig(rng) * 100.0f + 100.0f});
    std::sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return a.at < b.at; });

    auto render = [&](int block) {
        fx::FxSlot<TestEcho> echo;
        fx::FxSlot<fx::StereoWidth> width;
        echo.prepare(kFs, block);
        width.prepare(kFs, block);
        std::vector<float> L = inL, R = inR;
        std::size_t next = 0;
        for (std::size_t s = 0; s < kLen;) {
            while (next < changes.size() && changes[next].at <= s) {
                const Change& c = changes[next++];
                if (c.what == 0 || c.what == 1)
                    echo.setEnabled(c.what == 0);
                else if (c.what == 2 || c.what == 3)
                    width.setEnabled(c.what == 2);
                else
                    width.effect().setParams({.enabled = true, .widthPct = c.value});
            }
            // Stop at the next change or the block end, like the Engine does between events.
            std::size_t end = std::min(kLen, s + static_cast<std::size_t>(block));
            if (next < changes.size())
                end = std::min(end, std::max(s + 1, changes[next].at));
            const auto n = static_cast<int>(end - s);
            echo.process(L.data() + s, R.data() + s, n);
            width.process(L.data() + s, R.data() + s, n);
            s = end;
        }
        L.insert(L.end(), R.begin(), R.end());
        return L;
    };
    const auto ref = render(1000);
    for (int block : {1, 7, 64}) {
        INFO("block " << block);
        const auto got = render(block);
        REQUIRE(std::memcmp(got.data(), ref.data(), ref.size() * sizeof(float)) == 0);
    }
}

TEST_CASE("FxChain: all off is an exact pass-through except the output gain", "[fx]")
{
    fx::FxChain chain;
    chain.prepare(kFs, 128);
    chain.setParams({});
    chain.snapOutputDb(0.0f);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> sig(-1.0f, 1.0f);
    std::vector<float> L(1000), R(1000);
    for (std::size_t i = 0; i < L.size(); ++i) {
        L[i] = sig(rng);
        R[i] = sig(rng);
    }
    const auto l0 = L, r0 = R;
    chain.process(L.data(), R.data(), 1000); // longer than maxBlock: processed in pieces
    CHECK(L == l0);
    CHECK(R == r0);
    CHECK(chain.tailSamples() == 0);
    CHECK_FALSE(chain.ringing());
}

TEST_CASE("Engine: width on a mono voice changes nothing; the FX params reach the chain", "[fx][engine]")
{
    // CFS's voices are mono until L4.4, so width can't do anything yet: proof that it's wired, and that its
    // mid/side round trip is (nearly) transparent.
    auto render = [](bool widthOn) {
        Engine eng;
        eng.prepare(kFs, 256);
        synth::Patch p;
        p.fx.width = {.enabled = widthOn, .widthPct = 200.0f};
        eng.setPatch(p);
        std::vector<float> L(4800), R(4800);
        const Event on = Event::noteOn(0, 60, 1.0f);
        eng.process({&on, 1}, L.data(), R.data(), 4800);
        return std::pair{L, R};
    };
    const auto [l0, r0] = render(false);
    const auto [l1, r1] = render(true);
    float diff = 0.0f;
    for (std::size_t i = 0; i < l0.size(); ++i) {
        diff = std::max(diff, std::abs(l1[i] - l0[i]));
        CHECK(l1[i] == r1[i]); // still mono
    }
    CHECK(diff < 1e-6f);
}
