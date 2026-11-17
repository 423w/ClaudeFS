// L1.1 scenarios: the first renders, made with an inline formula (no oscillator yet), used to check the
// WAV writer, hear clipping, and study clicks. Kept as-is so L1.1's journal stays reproducible.
#include "render_util.h"
#include "scenarios.h"

#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace fss::render {

namespace {

// x[n] = amplitude · sin(2π · f · n / fs + phase0): the L1.1 test signal.
// The argument is computed in double from the integer sample index n. In float, 2π·440·n/fs reaches
// ~5500 radians after 2 s, where float spacing is ~0.0005 rad. That's visible phase noise.
// L1.2's Phasor replaces this formula (and explains why it's fragile).
std::vector<float> formulaSine(double hz, double seconds, double amplitude, double phase0 = 0.0)
{
    const auto n = static_cast<std::size_t>(std::lround(seconds * kSampleRate));
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kSampleRate + phase0));
    return x;
}

} // namespace

void renderL1_1(const std::filesystem::path& dir)
{
    // 1. The first render: 440 Hz at half scale (−6 dBFS), 2 s. Float, then the same in 16-bit.
    const auto a440 = formulaSine(440.0, 2.0, 0.5);
    renderMono(dir / "l1.1_sine440_f32.wav", a440, SampleFormat::Float32);
    renderMono(dir / "l1.1_sine440_s16.wav", a440, SampleFormat::Pcm16);

    // 2. The same sine at 1.5× full scale. Float keeps the overs; 16-bit must clip them.
    const auto hot = formulaSine(440.0, 2.0, 1.5);
    renderMono(dir / "l1.1_sine440_hot_f32.wav", hot, SampleFormat::Float32);
    renderMono(dir / "l1.1_sine440_clip_s16.wav", hot, SampleFormat::Pcm16);

    // 3. The click question, worst case: start at the waveform's peak (cosine phase) and stop mid-cycle,
    //    so both ends jump between silence and ±0.5 in one sample.
    const auto clicky = formulaSine(440.0, 1.0011, 0.5, std::numbers::pi / 2.0);
    renderMono(dir / "l1.1_click_f32.wav", clicky, SampleFormat::Float32);

    // 4. Stereo from planar buffers (the shape the Engine will produce): 440 Hz left, 660 Hz right.
    //    If L and R come out swapped in Python, the interleaving is wrong.
    const auto left = formulaSine(440.0, 1.0, 0.5);
    const auto right = formulaSine(660.0, 1.0, 0.25);
    const auto path = dir / "l1.1_stereo_f32.wav";
    WavWriter w(path, kSampleRate, 2, SampleFormat::Float32);
    const std::span<const float> channels[] = {left, right};
    w.writePlanar(channels);
    w.finish();
    std::printf("wrote %-40s %7llu frames (stereo: 440 Hz L, 660 Hz R)\n", path.string().c_str(),
                static_cast<unsigned long long>(w.framesWritten()));
}

} // namespace fss::render
