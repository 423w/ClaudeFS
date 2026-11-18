// state_v0.h: the plugin's first state format (L3.2): deliberately simple, so L3.2 could ship save/load and
// L3.4 could design the real format (D15) knowing what it has to stay compatible with.
//
// LEGACY since L3.4: CFS now SAVES text (synth::patchToText), but must LOAD this format forever, because FL
// Studio projects saved with the L3.2 build contain it. save() stays only so tests can make v0 states.
//
// Layout (all little-endian, which is what every x86/ARM Windows machine is):
//   bytes 0..3   magic "CFS0"
//   u32          format version = 0
//   u32          count N (≤ 4096)
//   N × { u32 param id, f32 plain value }
//
// Load rules: wrong magic / version / size → reject (return false, patch untouched). Unknown ids are skipped
// (a newer CFS may have saved parameters this one doesn't know). Missing ids keep their defaults. Values are
// clamped to range and NaN becomes the default (setParam + clampToRange do both).
#pragma once

#include <fss/synth/patch.h>

#include <cstddef>
#include <span>
#include <vector>

namespace fss::plugin::state_v0 {

inline constexpr char kMagic[4] = {'C', 'F', 'S', '0'};

[[nodiscard]] std::vector<std::byte> save(const synth::Patch& patch);
/// Parse `data` into a patch that starts from Patch{} defaults. Returns false (and leaves `out` alone) if
/// the data isn't a valid v0 state.
[[nodiscard]] bool load(std::span<const std::byte> data, synth::Patch& out);

} // namespace fss::plugin::state_v0
