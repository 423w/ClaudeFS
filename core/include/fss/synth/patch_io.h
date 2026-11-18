// fss/synth/patch_io.h: patches as text, for preset files and the plugin's saved state (L3.4, D15).
//
// A patch file looks like this (excerpt):
//
//     CFS patch 1
//     # CFS patch: "<parameter id> = <value>" per line, ...
//     name = E-piano
//
//     # Global
//     1 = 5            # Algorithm (5 two stacks: A>B*, C>D*)
//     2 = -13.152039   # Output [dB]
//     ...
//     # Operator A
//     1000 = 14        # Ratio [x]
//     ...
//     end
//
// RULES (they are what makes old and new files keep working):
//  - The first non-comment line is the header "CFS patch <format version>".
//  - Keys are the STABLE parameter ids of params.h, never names: names may be renamed, ids never change
//    meaning. A future change of meaning gets a NEW id, plus a migration in patch_io.cpp's migrate().
//  - Values are PLAIN (the param table's units) and written with the shortest text that reads back as
//    exactly the same float (std::to_chars), so save → load → save is byte-identical.
//  - Missing ids keep their defaults (the file predates the parameter). Unknown ids are skipped and
//    counted (the file comes from a newer CFS). Out-of-range values are clamped and counted.
//  - Anything malformed rejects the WHOLE file, with the line number. A half-loaded patch would sound wrong
//    without saying why; a clear error is better.
//  - The last line is "end". Without it the file is rejected: a text file cut short (a crash or a full disk
//    while saving) would otherwise still parse, just with the last parameters silently at their defaults.
//    (The L3.2 test host's "truncated state is rejected" check found that.) A checksum would catch more,
//    but would break every hand edit.
//  - Everything after '#' on a parameter line is a comment (names, units, value labels are written there
//    for humans; the loader never reads them).
//
// Pure functions on strings: no file I/O (that's the hosts' job: core never touches the OS).
#pragma once

#include <fss/synth/patch.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace fss::synth {

inline constexpr int kPatchFormatVersion = 1;   // version 0 was the L3.2 plugin's binary state (hosts/clap)
inline constexpr std::size_t kMaxPatchTextBytes = 64 * 1024; // a real file is ~3 KB; bigger = not ours

struct PatchLoad {
    bool ok = false;
    Patch patch{};
    std::string name;         // the "name = ..." line, or empty
    int version = 0;          // the file's format version
    std::string error;        // why it was rejected (ASCII, for consoles and host logs)
    int errorLine = 0;        // 1-based line of the problem, 0 if not line-specific
    int unknownIds = 0;       // skipped: parameters this build doesn't know (a newer CFS wrote the file)
    int clampedValues = 0;    // out of range, clamped into range
    int missingIds = 0;       // parameters the file didn't mention (kept their defaults)
};

/// The patch as text. Deterministic: the same patch and name always give the same bytes.
[[nodiscard]] std::string patchToText(const Patch& patch, std::string_view name = {});

/// Parse patch text. Never throws, never crashes on any input (tested with random and mutated bytes).
/// On failure `ok` is false, `error`/`errorLine` say why, and `patch` is Patch{} (callers keep theirs).
[[nodiscard]] PatchLoad patchFromText(std::string_view text);

} // namespace fss::synth
