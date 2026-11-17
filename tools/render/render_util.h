// render_util.h: small helpers shared by fss_render's lesson scenarios.
#pragma once

#include "wav_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <span>

namespace fss::render {

inline constexpr int kSampleRate = 48000; // CFS's default render rate

/// Write a mono buffer to `path`, then print a one-line summary (frames, peak, clipped, non-finite).
inline void renderMono(const std::filesystem::path& path, std::span<const float> x,
                       SampleFormat format = SampleFormat::Float32)
{
    WavWriter w(path, kSampleRate, 1, format);
    w.writeInterleaved(x);
    w.finish(); // explicit, so a failed write throws here instead of vanishing in the destructor

    float peak = 0.0f;
    for (float v : x)
        peak = std::max(peak, std::abs(v));
    std::printf("wrote %-40s %7llu frames, peak %.3f, clipped %llu, non-finite %llu\n", path.string().c_str(),
                static_cast<unsigned long long>(w.framesWritten()), peak,
                static_cast<unsigned long long>(w.clippedSamples()),
                static_cast<unsigned long long>(w.nonFiniteSamples()));
}

} // namespace fss::render
