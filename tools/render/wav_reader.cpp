#include "wav_reader.h"

#include "wav_bytes.h"

#include <bit>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

namespace fss::render {

namespace {

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& why)
{
    throw std::runtime_error("readWav: " + path.string() + ": " + why);
}

struct Fmt {
    std::uint16_t tag, channels, blockAlign, bits;
    std::uint32_t rate;
};

} // namespace

WavData readWav(const std::filesystem::path& path)
{
    // Slurp the whole file: renders are small, and parsing a byte array is simpler and safer than
    // seeking around a stream. Every read below is bounds-checked against b.size().
    std::ifstream in(path, std::ios::binary);
    if (!in)
        fail(path, "cannot open");
    const std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    if (b.size() < 12 || !tagIs(b, 0, "RIFF") || !tagIs(b, 8, "WAVE"))
        fail(path, "not a RIFF/WAVE file");

    // Walk the chunks: each is a 4-byte ID, a 4-byte size, the payload, and a pad byte if the size is odd.
    std::optional<Fmt> fmt;
    std::size_t dataAt = 0, dataSize = 0;
    bool haveData = false;
    std::size_t at = 12;
    while (at + 8 <= b.size()) {
        const std::size_t size = getU32(b, at + 4);
        const std::size_t body = at + 8;
        if (size > b.size() - body)
            fail(path, "chunk runs past the end of the file");

        if (tagIs(b, at, "fmt ")) {
            if (size < 16)
                fail(path, "fmt chunk too small");
            fmt = Fmt{getU16(b, body), getU16(b, body + 2), getU16(b, body + 12), getU16(b, body + 14),
                      getU32(b, body + 4)};
        } else if (tagIs(b, at, "data")) {
            dataAt = body;
            dataSize = size;
            haveData = true;
        }
        // Anything else (fact, LIST, bext, ...) is skipped.
        at = body + size + (size & 1); // the pad byte keeps every chunk on an even offset
    }

    if (!fmt)
        fail(path, "no fmt chunk");
    if (!haveData)
        fail(path, "no data chunk");

    SampleFormat format;
    int bytesPer;
    if (fmt->tag == 3 && fmt->bits == 32) {
        format = SampleFormat::Float32;
        bytesPer = 4;
    } else if (fmt->tag == 1 && fmt->bits == 16) {
        format = SampleFormat::Pcm16;
        bytesPer = 2;
    } else {
        fail(path, "unsupported format (tag " + std::to_string(fmt->tag) + ", " + std::to_string(fmt->bits) +
                       " bits); only 16-bit PCM and 32-bit float");
    }
    if (fmt->channels == 0 || fmt->blockAlign != fmt->channels * bytesPer)
        fail(path, "inconsistent channel count / block align");

    WavData out;
    out.sampleRate = static_cast<int>(fmt->rate);
    out.numChannels = fmt->channels;
    out.format = format;

    // Whole frames only: a truncated last frame (seen in damaged files) is dropped, not half-read.
    const std::size_t frames = dataSize / fmt->blockAlign;
    const std::size_t count = frames * fmt->channels;
    out.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t p = dataAt + i * static_cast<std::size_t>(bytesPer);
        if (format == SampleFormat::Float32)
            out.samples[i] = std::bit_cast<float>(getU32(b, p));
        else // reinterpret the 16 bits as two's complement, then scale like the writer did
            out.samples[i] = static_cast<float>(static_cast<std::int16_t>(getU16(b, p))) / 32767.0f;
    }
    return out;
}

} // namespace fss::render
