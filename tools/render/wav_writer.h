// wav_writer.h: write RIFF/WAVE files from float samples (L1.1).
//
// Lives in tools/render, not core: core never does I/O (CLAUDE.md). Hosts adapt the outside world (here: a
// file) to the engine's float samples.
//
// The file layout this writer produces (all integers little-endian):
//
//   offset  size  field                    16-bit PCM            32-bit float
//   ------  ----  -----------------------  --------------------  -----------------------------------
//        0     4  "RIFF"
//        4     4  RIFF size                file size − 8 (patched at finish)
//        8     4  "WAVE"
//       12     4  "fmt "
//       16     4  fmt chunk size           16                    18 (adds cbSize)
//       20     2  format tag               1 = PCM               3 = IEEE float
//       22     2  channels
//       24     4  sample rate (Hz)
//       28     4  byte rate                rate · blockAlign
//       32     2  block align              channels · bytes per sample = bytes per *frame*
//       34     2  bits per sample          16                    32
//       36     2  cbSize                   -                     0 (no extra format bytes)
//       ..        "fact" chunk             -                     4 bytes: frame count (patched)
//       ..        "data" chunk             size (patched), then the samples, interleaved: L R L R ...
//
// Sources: the classic "WAVE PCM soundfile format" page (CCRMA) for the PCM layout; Microsoft's
// WAVEFORMATEX docs for cbSize and the "fact" chunk, which the spec requires for non-PCM formats.
// (Most readers don't need fact, but a few strict ones do, and it costs 12 bytes.)
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace fss::render {

enum class SampleFormat : std::uint8_t {
    Float32, // IEEE float, format tag 3: exact, no clipping in the file, the default for renders
    Pcm16,   // 16-bit signed integer, format tag 1: what CDs use; values beyond ±1.0 are clipped
};

class WavWriter {
public:
    /// Creates (or truncates) `path` and writes a header with placeholder sizes.
    /// Throws std::runtime_error if the file can't be opened, std::invalid_argument for bad settings.
    WavWriter(const std::filesystem::path& path, int sampleRate, int numChannels, SampleFormat format);

    /// Safety net: calls finish() if you didn't, and swallows any error (destructors must not throw).
    /// Prefer calling finish() yourself so a failed write actually reaches you.
    ~WavWriter();

    // One writer owns one open file; copying would mean two objects patching the same header.
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    /// Append interleaved samples (L R L R ... for stereo). samples.size() must be a multiple of numChannels.
    void writeInterleaved(std::span<const float> samples);

    /// Append planar samples: one span per channel, all the same length (the shape an Engine produces).
    void writePlanar(std::span<const std::span<const float>> channels);

    /// Patch the RIFF/fact/data sizes, flush and close. Throws std::runtime_error on I/O failure.
    /// Safe to call more than once; writing after finish() throws std::logic_error.
    void finish();

    [[nodiscard]] std::uint64_t framesWritten() const noexcept { return frames_; }
    /// Samples with |x| > 1. In Pcm16 they were clamped; in Float32 they're stored as-is but will clip
    /// as soon as anything converts the file to fixed point.
    [[nodiscard]] std::uint64_t clippedSamples() const noexcept { return clipped_; }
    /// NaN or ±infinity samples: almost always a DSP bug upstream. Pcm16 writes NaN as 0 and ±inf as ±full scale.
    [[nodiscard]] std::uint64_t nonFiniteSamples() const noexcept { return nonFinite_; }

private:
    void encodeAppend(float x);   // one sample → bytes_ (format-specific)
    void flushBytes();            // bytes_ → file, with the 4 GiB size check
    void patchU32(std::streamoff offset, std::uint32_t value);

    std::filesystem::path path_;
    std::ofstream file_;
    int numChannels_;
    SampleFormat format_;

    std::streamoff riffSizeOffset_ = 4;
    std::streamoff factFramesOffset_ = -1; // only for Float32
    std::streamoff dataSizeOffset_ = 0;

    std::uint64_t dataBytes_ = 0;
    std::uint64_t frames_ = 0;
    std::uint64_t clipped_ = 0;
    std::uint64_t nonFinite_ = 0;
    bool finished_ = false;

    std::vector<unsigned char> bytes_; // scratch: encode a whole block, then one file_.write
};

} // namespace fss::render
