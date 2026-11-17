// wav_bytes.h: little-endian byte helpers shared by the WAV writer and reader (L1.1).
//
// WAV is little-endian: the least significant byte comes first. Writing value >> 0, >> 8, >> 16, >> 24
// byte by byte produces that order on *any* CPU, little- or big-endian, because shifts work on values,
// not on memory layout. Reading reassembles bytes the same way.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fss::render {

inline void putU16(std::vector<unsigned char>& out, std::uint16_t v)
{
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>(v >> 8));
}

inline void putU32(std::vector<unsigned char>& out, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<unsigned char>((v >> shift) & 0xFF));
}

/// A 4-character chunk ID such as "RIFF" or "fmt " (note the space: IDs are always exactly 4 bytes).
inline void putTag(std::vector<unsigned char>& out, std::string_view tag)
{
    for (char c : tag.substr(0, 4))
        out.push_back(static_cast<unsigned char>(c));
}

inline std::uint16_t getU16(std::span<const unsigned char> b, std::size_t at)
{
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

inline std::uint32_t getU32(std::span<const unsigned char> b, std::size_t at)
{
    return static_cast<std::uint32_t>(b[at]) | (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

inline bool tagIs(std::span<const unsigned char> b, std::size_t at, std::string_view tag)
{
    return at + 4 <= b.size() && std::string_view(reinterpret_cast<const char*>(b.data() + at), 4) == tag;
}

} // namespace fss::render
