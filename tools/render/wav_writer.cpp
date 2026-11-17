#include "wav_writer.h"

#include "wav_bytes.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace fss::render {

namespace {

// RIFF stores sizes in 32 bits, so a WAV file can't exceed 4 GiB (about 6 hours of 48 kHz stereo float).
// Formats like RF64/W64 lift that limit. Not needed for renders.
constexpr std::uint64_t kMaxRiffBytes = std::numeric_limits<std::uint32_t>::max();

int bytesPerSample(SampleFormat f) { return f == SampleFormat::Float32 ? 4 : 2; }

} // namespace

WavWriter::WavWriter(const std::filesystem::path& path, int sampleRate, int numChannels, SampleFormat format)
    : path_(path), numChannels_(numChannels), format_(format)
{
    // Mono and stereo only: Microsoft's spec wants WAVE_FORMAT_EXTENSIBLE (a bigger fmt chunk with a
    // speaker mask) for more than 2 channels. CFS renders mono or stereo, so the simple header is enough.
    if (numChannels < 1 || numChannels > 2)
        throw std::invalid_argument("WavWriter: numChannels must be 1 or 2");
    if (sampleRate <= 0)
        throw std::invalid_argument("WavWriter: sampleRate must be positive");

    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_)
        throw std::runtime_error("WavWriter: cannot open " + path.string());

    const bool isFloat = format == SampleFormat::Float32;
    const auto blockAlign = static_cast<std::uint16_t>(numChannels * bytesPerSample(format));

    // Build the header in memory with explicit little-endian byte writes, then write it in one go.
    // Why not a packed struct + reinterpret_cast? The compiler may insert padding between fields, the
    // host CPU might be big-endian, and there are two header layouts anyway. Byte-by-byte is portable
    // and shows exactly what lands where.
    std::vector<unsigned char> h;
    putTag(h, "RIFF");
    putU32(h, 0);                                  // RIFF size: placeholder, patched in finish()
    putTag(h, "WAVE");

    putTag(h, "fmt ");
    putU32(h, isFloat ? 18u : 16u);                // fmt chunk size
    putU16(h, isFloat ? 3u : 1u);                  // format tag
    putU16(h, static_cast<std::uint16_t>(numChannels));
    putU32(h, static_cast<std::uint32_t>(sampleRate));
    putU32(h, static_cast<std::uint32_t>(sampleRate) * blockAlign); // byte rate: bytes per second
    putU16(h, blockAlign);
    putU16(h, static_cast<std::uint16_t>(8 * bytesPerSample(format))); // bits per sample
    if (isFloat) {
        putU16(h, 0);                              // cbSize: no extension bytes follow
        putTag(h, "fact");
        putU32(h, 4);
        factFramesOffset_ = static_cast<std::streamoff>(h.size());
        putU32(h, 0);                              // frames: placeholder
    }

    putTag(h, "data");
    dataSizeOffset_ = static_cast<std::streamoff>(h.size());
    putU32(h, 0);                                  // data size: placeholder

    file_.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
    if (!file_)
        throw std::runtime_error("WavWriter: cannot write header to " + path.string());
}

WavWriter::~WavWriter()
{
    // RAII as a safety net: if an exception unwinds past the writer, the samples written so far still
    // end up in a valid file. But a destructor can't report failure (throwing from one during unwinding
    // calls std::terminate), so errors here are swallowed. That's why finish() exists too.
    try {
        finish();
    } catch (...) {
    }
}

void WavWriter::encodeAppend(float x)
{
    // Statistics first, on the value we were *given*.
    if (!std::isfinite(x)) {
        ++nonFinite_;
    } else if (std::abs(x) > 1.0f) {
        ++clipped_;
    }

    if (format_ == SampleFormat::Float32) {
        // Store the float's IEEE-754 bit pattern, little-endian. std::bit_cast (C++20) is the defined way
        // to reinterpret bits; type-punning through a union or pointer cast is undefined behaviour.
        static_assert(std::numeric_limits<float>::is_iec559, "WAV float format requires IEEE-754 floats");
        putU32(bytes_, std::bit_cast<std::uint32_t>(x));
        return;
    }

    // Pcm16. NaN has no sensible integer value; silence is the least harmful. (std::clamp on NaN would
    // return NaN, and converting NaN to an integer is undefined behaviour, so this check must come first.)
    float s = std::isnan(x) ? 0.0f : std::clamp(x, -1.0f, 1.0f); // ±inf clamps to ±1 like any big value

    // Scale by 32767, not 32768. Symmetric: +1.0 → +32767 and −1.0 → −32767, so a full-scale sine stays
    // centred and +1.0 can't overflow. The cost: the code −32768 is never used (0.0003 dB of headroom).
    // lrint rounds to nearest (ties to even), which halves the error compared with truncation.
    const auto v = static_cast<std::int16_t>(std::lrint(s * 32767.0f));
    putU16(bytes_, static_cast<std::uint16_t>(v)); // two's complement bits, little-endian
}

void WavWriter::writeInterleaved(std::span<const float> samples)
{
    if (finished_)
        throw std::logic_error("WavWriter: write after finish()");
    if (samples.size() % static_cast<std::size_t>(numChannels_) != 0)
        throw std::invalid_argument("WavWriter: sample count is not a multiple of the channel count");

    bytes_.clear();
    bytes_.reserve(samples.size() * static_cast<std::size_t>(bytesPerSample(format_)));
    for (float x : samples)
        encodeAppend(x);
    flushBytes();
    frames_ += samples.size() / static_cast<std::size_t>(numChannels_);
}

void WavWriter::writePlanar(std::span<const std::span<const float>> channels)
{
    if (finished_)
        throw std::logic_error("WavWriter: write after finish()");
    if (channels.size() != static_cast<std::size_t>(numChannels_))
        throw std::invalid_argument("WavWriter: writePlanar needs one span per channel");
    const std::size_t n = channels[0].size();
    for (const auto& ch : channels)
        if (ch.size() != n)
            throw std::invalid_argument("WavWriter: planar channels have different lengths");

    // Interleave on the fly: frame by frame, channel by channel. This loop *is* the planar → interleaved
    // conversion: L[0] R[0] L[1] R[1] ...
    bytes_.clear();
    bytes_.reserve(n * channels.size() * static_cast<std::size_t>(bytesPerSample(format_)));
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& ch : channels)
            encodeAppend(ch[i]);
    flushBytes();
    frames_ += n;
}

void WavWriter::flushBytes()
{
    // The header counts too: RIFF size = everything after the first 8 bytes, which must fit in 32 bits.
    const auto headerBytes = static_cast<std::uint64_t>(dataSizeOffset_) + 4;
    if (headerBytes - 8 + dataBytes_ + bytes_.size() > kMaxRiffBytes)
        throw std::runtime_error("WavWriter: file would exceed the 4 GiB RIFF limit");

    file_.write(reinterpret_cast<const char*>(bytes_.data()), static_cast<std::streamsize>(bytes_.size()));
    if (!file_)
        throw std::runtime_error("WavWriter: write failed for " + path_.string());
    dataBytes_ += bytes_.size();
}

void WavWriter::patchU32(std::streamoff offset, std::uint32_t value)
{
    std::vector<unsigned char> b;
    putU32(b, value);
    file_.seekp(offset);
    file_.write(reinterpret_cast<const char*>(b.data()), 4);
}

void WavWriter::finish()
{
    if (finished_)
        return;
    finished_ = true; // set first: if patching throws, the destructor must not try again

    if (!file_) { // an earlier write failed: the stream can't seek or tell, so don't pretend
        file_.close();
        throw std::runtime_error("WavWriter: stream already failed for " + path_.string());
    }

    // RIFF chunks must start on even offsets, so an odd-sized chunk gets one pad byte (not counted in
    // its size). Our sample sizes are 2 or 4 bytes, so data is always even. The check documents the rule.
    if (dataBytes_ % 2 != 0)
        file_.put('\0');

    const auto end = static_cast<std::uint64_t>(file_.tellp());
    patchU32(riffSizeOffset_, static_cast<std::uint32_t>(end - 8));
    patchU32(dataSizeOffset_, static_cast<std::uint32_t>(dataBytes_));
    if (factFramesOffset_ >= 0)
        patchU32(factFramesOffset_, static_cast<std::uint32_t>(frames_));

    file_.flush();
    const bool ok = static_cast<bool>(file_);
    file_.close();
    if (!ok || file_.fail())
        throw std::runtime_error("WavWriter: failed to finalize " + path_.string());
}

} // namespace fss::render
