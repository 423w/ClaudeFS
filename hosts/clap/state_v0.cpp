// state_v0.cpp: see state_v0.h. Byte order is written out explicitly (shifts, not memcpy of a struct), so the
// format doesn't depend on the compiler's struct padding or the CPU's endianness.
#include "state_v0.h"

#include <fss/synth/params.h>

#include <bit>
#include <cstdint>
#include <cstring>

namespace fss::plugin::state_v0 {

namespace {

void putU32(std::vector<std::byte>& out, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::byte>((v >> shift) & 0xFFu));
}

std::uint32_t getU32(std::span<const std::byte> in, std::size_t at)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(in[at + static_cast<std::size_t>(i)]) << (8 * i);
    return v;
}

} // namespace

std::vector<std::byte> save(const synth::Patch& patch)
{
    std::vector<std::byte> out;
    out.reserve(12 + 8 * synth::kParams.size());
    for (char c : kMagic)
        out.push_back(static_cast<std::byte>(c));
    putU32(out, 0); // version
    putU32(out, static_cast<std::uint32_t>(synth::kParams.size()));
    for (const synth::ParamInfo& p : synth::kParams) {
        putU32(out, p.id);
        putU32(out, std::bit_cast<std::uint32_t>(synth::getParam(patch, p))); // the float's exact bits
    }
    return out;
}

bool load(std::span<const std::byte> data, synth::Patch& out)
{
    if (data.size() < 12 || std::memcmp(data.data(), kMagic, 4) != 0)
        return false;
    if (getU32(data, 4) != 0)
        return false;
    const std::uint32_t count = getU32(data, 8);
    // Check the size BEFORE trusting the count: a corrupt count must not make us read past the end.
    if (count > 4096 || data.size() != 12 + 8 * static_cast<std::size_t>(count))
        return false;
    synth::Patch patch{};
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 12 + 8 * static_cast<std::size_t>(i);
        const synth::ParamInfo* p = synth::findParam(getU32(data, at));
        if (p)
            synth::setParam(patch, *p, std::bit_cast<float>(getU32(data, at + 4)));
    }
    out = patch;
    return true;
}

} // namespace fss::plugin::state_v0
