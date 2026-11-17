// Tests for the WAV writer and reader (L1.1, tools/render).
//
// Three independent layers of checking, so the writer and reader can't "agree on a bug":
//   1. The header test compares raw bytes against a file worked out by hand from the spec.
//      It doesn't use the reader at all.
//   2. Round trips (write → readWav) check the payload and the reader together.
//   3. Outside C++: scipy, Python's `wave` module and tools/analyze/wavinfo.py read the renders
//      (see the L1.1 journal).
#include "wav_reader.h"
#include "wav_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <vector>

using namespace fss::render;
using Catch::Matchers::WithinAbs;

namespace {

// A file in the system temp folder, deleted when the test ends. Each test uses its own name, so tests
// can run in parallel (ctest -j) without stepping on each other.
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("cfs_test_" + name + ".wav"))
    {
        std::filesystem::remove(path);
    }
    ~TempFile() { std::filesystem::remove(path); }
};

std::vector<unsigned char> readBytes(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t u32(const std::vector<unsigned char>& b, std::size_t at)
{
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}
std::uint16_t u16(const std::vector<unsigned char>& b, std::size_t at)
{
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}
std::string tag(const std::vector<unsigned char>& b, std::size_t at)
{
    return std::string(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at) + 4);
}

} // namespace

TEST_CASE("WavWriter: 16-bit mono header is byte-exact", "[wav][header]")
{
    TempFile f("pcm16_bytes");
    {
        WavWriter w(f.path, 48000, 1, SampleFormat::Pcm16);
        const float x[] = {0.0f, 0.5f, -1.0f};
        w.writeInterleaved(x);
        w.finish();
    }
    // Worked out by hand from the spec. 44-byte header + 3 samples × 2 bytes = 50 bytes.
    const std::vector<unsigned char> expected = {
        'R', 'I', 'F', 'F', 42, 0, 0, 0,          // RIFF size = 50 − 8
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0,          // fmt chunk: 16 bytes
        1, 0,                                     // format tag 1 = PCM
        1, 0,                                     // 1 channel
        0x80, 0xBB, 0, 0,                         // 48000 = 0x0000BB80, little-endian
        0x00, 0x77, 0x01, 0,                      // byte rate 96000 = 0x00017700
        2, 0,                                     // block align: 1 ch × 2 bytes
        16, 0,                                    // bits per sample
        'd', 'a', 't', 'a', 6, 0, 0, 0,           // data: 6 bytes
        0x00, 0x00,                               //  0.0  →      0
        0x00, 0x40,                               //  0.5  → 16384 (0.5 × 32767 = 16383.5, ties-to-even rounds up)
        0x01, 0x80,                               // −1.0  → −32767 = 0x8001 (symmetric scaling)
    };
    CHECK(readBytes(f.path) == expected);
}

TEST_CASE("WavWriter: float stereo header fields", "[wav][header]")
{
    TempFile f("f32_header");
    constexpr int kFrames = 100;
    {
        WavWriter w(f.path, 44100, 2, SampleFormat::Float32);
        std::vector<float> x(2 * kFrames, 0.25f);
        w.writeInterleaved(x);
        w.finish();
        CHECK(w.framesWritten() == kFrames);
    }
    const auto b = readBytes(f.path);
    REQUIRE(b.size() == 58 + kFrames * 8);  // header: 12 + fmt(8+18) + fact(8+4) + data header 8 = 58

    CHECK(tag(b, 0) == "RIFF");
    CHECK(u32(b, 4) == b.size() - 8);
    CHECK(tag(b, 8) == "WAVE");
    CHECK(tag(b, 12) == "fmt ");
    CHECK(u32(b, 16) == 18);            // fmt size includes cbSize for non-PCM formats
    CHECK(u16(b, 20) == 3);             // IEEE float
    CHECK(u16(b, 22) == 2);             // channels
    CHECK(u32(b, 24) == 44100);         // sample rate
    CHECK(u32(b, 28) == 44100 * 8);     // byte rate
    CHECK(u16(b, 32) == 8);             // block align: 2 ch × 4 bytes
    CHECK(u16(b, 34) == 32);            // bits
    CHECK(u16(b, 36) == 0);             // cbSize
    CHECK(tag(b, 38) == "fact");
    CHECK(u32(b, 42) == 4);
    CHECK(u32(b, 46) == kFrames);       // fact: frame count
    CHECK(tag(b, 50) == "data");
    CHECK(u32(b, 54) == kFrames * 8);   // data size in bytes
}

TEST_CASE("WavWriter + readWav: float round trip is bit-exact", "[wav][roundtrip]")
{
    TempFile f("f32_roundtrip");
    // Awkward values on purpose: −0, a denormal, overs beyond ±1, the largest float, infinity.
    const std::vector<float> x = {0.0f, -0.0f, 1e-40f, 0.123456789f, -1.0f, 1.5f, -7.25f,
                                  std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity()};
    {
        WavWriter w(f.path, 48000, 1, SampleFormat::Float32);
        w.writeInterleaved(x);
        w.finish();
        CHECK(w.clippedSamples() == 3);    // 1.5, −7.25, max (stored as-is, but flagged)
        CHECK(w.nonFiniteSamples() == 1);  // +inf
    }
    const WavData d = readWav(f.path);
    CHECK(d.sampleRate == 48000);
    CHECK(d.numChannels == 1);
    CHECK(d.format == SampleFormat::Float32);
    REQUIRE(d.samples.size() == x.size());
    for (std::size_t i = 0; i < x.size(); ++i) // compare bit patterns: tells −0 from +0, survives inf
        CHECK(std::bit_cast<std::uint32_t>(d.samples[i]) == std::bit_cast<std::uint32_t>(x[i]));
}

TEST_CASE("WavWriter + readWav: 16-bit round trip within half a step", "[wav][roundtrip]")
{
    TempFile f("pcm16_roundtrip");
    std::vector<float> x;
    for (int i = -1000; i <= 1000; ++i)
        x.push_back(static_cast<float>(i) / 1000.0f); // −1 … +1 in 0.001 steps
    {
        WavWriter w(f.path, 48000, 1, SampleFormat::Pcm16);
        w.writeInterleaved(x);
        w.finish();
        CHECK(w.clippedSamples() == 0);
    }
    const WavData d = readWav(f.path);
    REQUIRE(d.samples.size() == x.size());
    // Rounding to the nearest of 65535 levels: error ≤ half a step = 0.5/32767 (+ float slack).
    for (std::size_t i = 0; i < x.size(); ++i)
        CHECK_THAT(d.samples[i], WithinAbs(x[i], 0.5 / 32767.0 + 1e-7));
}

TEST_CASE("WavWriter: 16-bit clamps overs, maps NaN to silence, and counts both", "[wav][clip]")
{
    TempFile f("pcm16_clip");
    const float inf = std::numeric_limits<float>::infinity();
    const float x[] = {1.5f, -2.0f, inf, -inf, std::numeric_limits<float>::quiet_NaN(), 1.0f};
    WavWriter w(f.path, 48000, 1, SampleFormat::Pcm16);
    w.writeInterleaved(x);
    w.finish();
    CHECK(w.clippedSamples() == 2);    // 1.5, −2 (±1.0 exactly is not a clip)
    CHECK(w.nonFiniteSamples() == 3);  // +inf, −inf, NaN

    const auto b = readBytes(f.path);
    REQUIRE(b.size() == 44 + 6 * 2);
    const auto s = [&](int i) { return static_cast<std::int16_t>(u16(b, 44 + 2 * static_cast<std::size_t>(i))); };
    CHECK(s(0) == 32767);
    CHECK(s(1) == -32767);
    CHECK(s(2) == 32767);
    CHECK(s(3) == -32767);
    CHECK(s(4) == 0);
    CHECK(s(5) == 32767);
}

TEST_CASE("WavWriter: planar and interleaved writes produce identical files", "[wav][layout]")
{
    TempFile a("planar"), b("interleaved");
    const std::vector<float> left = {0.1f, 0.2f, 0.3f}, right = {-0.1f, -0.2f, -0.3f};
    {
        WavWriter w(a.path, 48000, 2, SampleFormat::Float32);
        const std::span<const float> ch[] = {left, right};
        w.writePlanar(ch);
        w.finish();
    }
    {
        WavWriter w(b.path, 48000, 2, SampleFormat::Float32);
        const float inter[] = {0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f}; // L R L R L R
        w.writeInterleaved(inter);
        w.finish();
    }
    CHECK(readBytes(a.path) == readBytes(b.path));
    const WavData d = readWav(a.path);
    CHECK(d.numFrames() == 3);
    CHECK(d.samples[0] == 0.1f);  // frame 0, left
    CHECK(d.samples[1] == -0.1f); // frame 0, right
}

TEST_CASE("WavWriter: several writes append, and the destructor finishes the file", "[wav][raii]")
{
    TempFile f("raii");
    {
        WavWriter w(f.path, 48000, 1, SampleFormat::Float32);
        const float x[] = {0.5f, 0.25f};
        w.writeInterleaved(x);
        w.writeInterleaved(x);
        // no finish(): the destructor must patch the sizes
    }
    const WavData d = readWav(f.path);
    REQUIRE(d.numFrames() == 4);
    CHECK(d.samples[3] == 0.25f);
    const auto b = readBytes(f.path);
    CHECK(u32(b, 4) == b.size() - 8);
}

TEST_CASE("WavWriter: an empty file is still a valid WAV", "[wav][edge]")
{
    TempFile f("empty");
    {
        WavWriter w(f.path, 48000, 2, SampleFormat::Pcm16);
        w.finish();
    }
    const WavData d = readWav(f.path);
    CHECK(d.numFrames() == 0);
    CHECK(d.numChannels == 2);
}

TEST_CASE("WavWriter: rejects bad arguments and use after finish", "[wav][errors]")
{
    TempFile f("errors");
    CHECK_THROWS_AS(WavWriter(f.path, 48000, 3, SampleFormat::Float32), std::invalid_argument);
    CHECK_THROWS_AS(WavWriter(f.path, 0, 1, SampleFormat::Float32), std::invalid_argument);

    WavWriter w(f.path, 48000, 2, SampleFormat::Float32);
    const float odd[] = {0.0f, 0.0f, 0.0f}; // 3 samples can't be whole stereo frames
    CHECK_THROWS_AS(w.writeInterleaved(odd), std::invalid_argument);
    w.finish();
    CHECK_NOTHROW(w.finish()); // idempotent
    const float two[] = {0.0f, 0.0f};
    CHECK_THROWS_AS(w.writeInterleaved(two), std::logic_error);
}

TEST_CASE("readWav: skips unknown chunks, including odd-sized ones with a pad byte", "[wav][reader]")
{
    TempFile f("foreign");
    // Hand-built: RIFF, WAVE, a 3-byte "junk" chunk (+1 pad byte), fmt (PCM16 mono 8 kHz), data (2 samples).
    const std::vector<unsigned char> bytes = {
        'R', 'I', 'F', 'F', 52, 0, 0, 0, 'W', 'A', 'V', 'E',
        'j', 'u', 'n', 'k', 3, 0, 0, 0, 'x', 'y', 'z', 0,     // odd size → pad byte
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1F, 0, 0, 0x80, 0x3E, 0, 0, 2, 0, 16, 0,
        'd', 'a', 't', 'a', 4, 0, 0, 0, 0xFF, 0x7F, 0x01, 0x80,  // +32767, −32767
    };
    {
        std::ofstream out(f.path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const WavData d = readWav(f.path);
    CHECK(d.sampleRate == 8000);
    REQUIRE(d.samples.size() == 2);
    CHECK(d.samples[0] == 1.0f);
    CHECK(d.samples[1] == -1.0f);
}

TEST_CASE("readWav: clear errors for files it can't read", "[wav][reader]")
{
    TempFile f("garbage");
    CHECK_THROWS_AS(readWav(f.path), std::runtime_error); // missing
    {
        std::ofstream out(f.path, std::ios::binary);
        out << "this is not a wav file";
    }
    CHECK_THROWS_AS(readWav(f.path), std::runtime_error);
}
