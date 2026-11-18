// L2.4 scenarios: parameter smoothing, A/B.
//
// A knob turned in a GUI sends ~60 values per second, i.e. a staircase. These renders send exactly that
// staircase as ParamChange events, once with the Engine's smoothing on and once off:
//   l2.4_level_{smooth,zipper}.wav    a pure 440 Hz sine whose LEVEL is swept 1 → 0.03 → 1 in 60 Hz steps.
//                                      Pure sine: any energy above 2 kHz is zipper noise.
//   l2.4_index_{smooth,zipper}.wav    a 1:1 FM tone whose modulator INDEX is swept 0 → 4 → 0 in 60 Hz steps
//                                      (a brightness knob), for listening.
//   l2.4_algorithm_{smooth,zipper}.wav  e-piano chord, algorithm switched 5 → 1 → 8 → 5 every 0.5 s
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/synth/params.h>
#include <fss/util/math.h>
#include <fss/synth/presets.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace fss::render {

namespace {

using synth::OpField;
using synth::param_id::op;

/// Render `seconds` of the Engine with events produced by `eventsAt(sample)` at each 60 Hz "GUI frame".
StereoBuffer renderAutomation(const synth::Patch& patch, bool smoothing, double seconds,
                              const std::vector<std::uint8_t>& keys,
                              const std::function<void(int frame, std::vector<Event>&)>& guiFrame)
{
    Engine eng;
    eng.setSmoothingEnabled(smoothing);
    eng.prepare(kSampleRate, 256);
    eng.setPatch(patch);
    const auto total = static_cast<std::size_t>(seconds * kSampleRate);
    StereoBuffer out;
    out.left.resize(total);
    out.right.resize(total);
    constexpr int kGuiPeriod = kSampleRate / 60; // 800 samples: one GUI frame
    std::vector<Event> ev;
    for (std::size_t start = 0, frame = 0; start < total; start += kGuiPeriod, ++frame) {
        ev.clear();
        if (frame == 0)
            for (std::uint8_t k : keys)
                ev.push_back(Event::noteOn(0, k, 0.9f));
        guiFrame(static_cast<int>(frame), ev);
        const auto n = static_cast<int>(std::min<std::size_t>(kGuiPeriod, total - start));
        eng.process(ev, out.left.data() + start, out.right.data() + start, n);
    }
    return out;
}

/// A triangle 0 → 1 → 0 over `frames` GUI frames.
float tri(int frame, int frames)
{
    const float x = static_cast<float>(frame % frames) / static_cast<float>(frames);
    return x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x;
}

} // namespace

void renderL2_4(const std::filesystem::path& dir)
{
    // 1. Level staircase on a pure sine (algorithm 8, only op A sounding).
    synth::Patch sine;
    sine.algorithm = synth::kAlgorithms[7];
    sine.algorithmNumber = 8;
    sine.algorithm.carrier = {1.0f, 0.0f, 0.0f, 0.0f};
    sine.ops[0].env = {.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 50.0f};
    for (int i : {1, 2, 3})
        sine.ops[static_cast<std::size_t>(i)].level = 0.0f;
    sine.outputDb = util::gainToDb(0.5f);
    for (bool smooth : {true, false}) {
        const auto buf = renderAutomation(sine, smooth, 3.0, {69}, [](int f, std::vector<Event>& ev) {
            const float level = 1.0f - 0.97f * tri(f, 90); // 1.5 s per sweep down and up
            ev.push_back(Event::paramChange(0, op(0, OpField::Level), level));
        });
        writeStereo(dir / (smooth ? "l2.4_level_smooth.wav" : "l2.4_level_zipper.wav"), buf);
    }

    // 2. Index staircase on a 1:1 FM tone (C > D of the stack).
    synth::Patch fm;
    fm.ops[0].level = fm.ops[1].level = 0.0f;
    fm.ops[2].level = 0.0f;
    fm.ops[2].env = fm.ops[3].env = {.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 50.0f};
    fm.outputDb = util::gainToDb(0.4f);
    for (bool smooth : {true, false}) {
        const auto buf = renderAutomation(fm, smooth, 3.0, {45}, [](int f, std::vector<Event>& ev) {
            ev.push_back(Event::paramChange(0, op(2, OpField::Level), 4.0f * tri(f, 90)));
        });
        writeStereo(dir / (smooth ? "l2.4_index_smooth.wav" : "l2.4_index_zipper.wav"), buf);
    }

    // 3. Algorithm switches under a held e-piano chord: smoothing morphs the matrix instead of jumping.
    for (bool smooth : {true, false}) {
        const auto buf = renderAutomation(synth::presets::epiano(), smooth, 2.5, {57, 60, 64}, [](int f, std::vector<Event>& ev) {
            static constexpr float kSeq[] = {5.0f, 1.0f, 8.0f, 5.0f};
            if (f > 0 && f % 30 == 0 && f / 30 < 4)
                ev.push_back(Event::paramChange(0, synth::param_id::kAlgorithm, kSeq[f / 30]));
        });
        writeStereo(dir / (smooth ? "l2.4_algorithm_smooth.wav" : "l2.4_algorithm_zipper.wav"), buf);
    }
}

} // namespace fss::render
