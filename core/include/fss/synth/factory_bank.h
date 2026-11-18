// fss/synth/factory_bank.h: the factory presets, as the text of presets/factory/*.cfspatch (L3.4).
//
// The .cfspatch files in the repo are the source of truth users see: readable, diffable, copyable into
// their own preset folder. The build embeds their text into fss_core (core/CMakeLists.txt generates
// factory_bank.cpp at configure time), so every host has the bank without shipping files next to the
// plugin. Parse an entry with synth::patchFromText. A test parses all of them on every build.
#pragma once

#include <span>
#include <string_view>

namespace fss::synth {

struct FactoryPreset {
    std::string_view name; // from the file name, without the "NN " sort prefix: "E-piano"
    std::string_view text; // the whole file
};

/// In file-name order (the "NN " prefix sets it).
[[nodiscard]] std::span<const FactoryPreset> factoryBank() noexcept;

} // namespace fss::synth
