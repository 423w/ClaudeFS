// preset_library.h: the presets a user can pick from, for every host with a GUI (L3.4): the factory bank
// (embedded in fss_core) plus the user's own .cfspatch files in Documents\CFS Presets.
//
// File I/O lives here, not in core: core only turns text into patches (synth::patchFromText). Main thread
// only (it reads the disk); never call it from an audio callback.
#pragma once

#include <fss/synth/patch.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fss::preset_lib {

struct PresetEntry {
    std::string name;
    bool factory = false;
    std::filesystem::path path; // user presets only
    std::string_view text;      // factory presets only (points into the embedded bank)
};

/// %USERPROFILE%\Documents\CFS Presets (not created until the first save).
[[nodiscard]] std::filesystem::path userPresetDir();

/// Factory presets (bank order), then the user's *.cfspatch files (alphabetical). Re-scans the folder.
[[nodiscard]] std::vector<PresetEntry> listPresets();

/// Load one entry. On failure returns false and fills `error` (the patch is left untouched).
bool loadPreset(const PresetEntry& entry, synth::Patch& patch, std::string& name, std::string& error);

/// Save as Documents\CFS Presets\<name>.cfspatch (characters Windows forbids in file names become '_').
/// Overwrites a file of the same name. Returns false and fills `error` on failure.
bool savePreset(const synth::Patch& patch, std::string_view name, std::string& error);

} // namespace fss::preset_lib
