// fss_render: offline renderer (host).
//
//   fss_render [--out DIR]        renders the current lesson's scenarios into DIR (default: renders)
//
// Hosts are the only code allowed to do I/O (CLAUDE.md). For now the "synth" is a formula written
// inline here. L1.2 replaces it with fss_core's phase-accumulator oscillator, and later lessons replace
// this whole file's guts with Engine::process().

#include "wav_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fss::render::SampleFormat;
using fss::render::WavWriter;

constexpr int kSampleRate = 48000;

// x[n] = amplitude · sin(2π · f · n / fs + phase0), the L1.1 test signal.
// The argument is computed in double from the integer sample index n. In float, 2π·440·n/fs reaches
// ~5500 radians after 2 s, where float spacing is ~0.0005 rad. That's visible phase noise. (L0.2 found
// the same trap.) L1.2's oscillator avoids the issue differently: it keeps a wrapped phase.
std::vector<float> sine(double hz, double seconds, double amplitude, double phase0 = 0.0)
{
    const auto n = static_cast<std::size_t>(std::lround(seconds * kSampleRate));
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kSampleRate + phase0));
    return x;
}

void report(const std::filesystem::path& path, const WavWriter& w, std::span<const float> x)
{
    float peak = 0.0f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    std::printf("wrote %-40s %7llu frames, peak %.3f, clipped %llu, non-finite %llu\n",
                path.string().c_str(), static_cast<unsigned long long>(w.framesWritten()), peak,
                static_cast<unsigned long long>(w.clippedSamples()),
                static_cast<unsigned long long>(w.nonFiniteSamples()));
}

void renderMono(const std::filesystem::path& path, std::span<const float> x, SampleFormat format)
{
    WavWriter w(path, kSampleRate, 1, format);
    w.writeInterleaved(x);
    w.finish(); // explicit, so a failed write throws here instead of vanishing in the destructor
    report(path, w, x);
}

void renderL1_1(const std::filesystem::path& dir)
{
    // 1. The first render: 440 Hz at half scale (−6 dBFS), 2 s. Float, then the same in 16-bit.
    const auto a440 = sine(440.0, 2.0, 0.5);
    renderMono(dir / "l1.1_sine440_f32.wav", a440, SampleFormat::Float32);
    renderMono(dir / "l1.1_sine440_s16.wav", a440, SampleFormat::Pcm16);

    // 2. The same sine at 1.5× full scale. Float keeps the overs; 16-bit must clip them.
    const auto hot = sine(440.0, 2.0, 1.5);
    renderMono(dir / "l1.1_sine440_hot_f32.wav", hot, SampleFormat::Float32);
    renderMono(dir / "l1.1_sine440_clip_s16.wav", hot, SampleFormat::Pcm16);

    // 3. The click question, worst case: start at the waveform's peak (cosine phase) and stop mid-cycle,
    //    so both ends jump between silence and ±0.5 in one sample.
    const auto clicky = sine(440.0, 1.0011, 0.5, std::numbers::pi / 2.0);
    renderMono(dir / "l1.1_click_f32.wav", clicky, SampleFormat::Float32);

    // 4. Stereo from planar buffers (the shape the Engine will produce): 440 Hz left, 660 Hz right.
    //    If L and R come out swapped in Python, the interleaving is wrong.
    const auto left = sine(440.0, 1.0, 0.5);
    const auto right = sine(660.0, 1.0, 0.25);
    const auto path = dir / "l1.1_stereo_f32.wav";
    WavWriter w(path, kSampleRate, 2, SampleFormat::Float32);
    const std::span<const float> channels[] = {left, right};
    w.writePlanar(channels);
    w.finish();
    report(path, w, left);
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path outDir = "renders";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else {
            std::fprintf(stderr, "usage: fss_render [--out DIR]\n");
            return 2;
        }
    }

    try {
        std::filesystem::create_directories(outDir);
        renderL1_1(outDir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fss_render: %s\n", e.what());
        return 1;
    }
    return 0;
}
