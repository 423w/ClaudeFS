// wav_reader.h: read the WAV files CFS writes, and well-formed ones like them (L1.1 stretch).
//
// Supports what WavWriter produces: PCM 16-bit (tag 1) and IEEE float 32-bit (tag 3), any channel
// count, and skips chunks it doesn't know (LIST, bext, cue, ... are common in DAW exports). Anything else
// (24-bit, WAVE_FORMAT_EXTENSIBLE, compressed) throws: better a clear error than wrong numbers.
//
// Uses: the round-trip unit tests (L1.1), golden-file tests (L6.1), loading wavetables (L4.3).
#pragma once

#include "wav_writer.h" // SampleFormat

#include <cstddef>
#include <filesystem>
#include <vector>

namespace fss::render {

struct WavData {
    int sampleRate = 0;
    int numChannels = 0;
    SampleFormat format = SampleFormat::Float32;
    std::vector<float> samples; // interleaved, on the ±1.0 scale (16-bit is divided by 32767)

    [[nodiscard]] std::size_t numFrames() const noexcept
    {
        return numChannels > 0 ? samples.size() / static_cast<std::size_t>(numChannels) : 0;
    }
};

/// Read a whole WAV file into memory. Throws std::runtime_error with a reason if the file is
/// missing, malformed, or in a format this reader doesn't support.
[[nodiscard]] WavData readWav(const std::filesystem::path& path);

} // namespace fss::render
