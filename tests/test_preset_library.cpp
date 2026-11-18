// Tests for the preset library (L3.4): save, list, load, and bad files on disk. CFS_PRESET_DIR points it at
// a temporary folder, so the tests never touch the real Documents\CFS Presets.
#include "preset_library.h"

#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace fss;

TEST_CASE("preset library: save, list, load; bad files fail with a message", "[presets]")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "cfs_preset_test";
    std::filesystem::remove_all(dir);
    REQUIRE(_putenv_s("CFS_PRESET_DIR", dir.string().c_str()) == 0);
    CHECK(preset_lib::userPresetDir() == dir);

    std::string error;
    CHECK_FALSE(preset_lib::savePreset(synth::Patch{}, "  ", error)); // no name
    CHECK_FALSE(error.empty());
    REQUIRE(preset_lib::savePreset(synth::presets::marimba(), "My: marimba?", error)); // ':' and '?' become '_'
    CHECK(std::filesystem::exists(dir / "My_ marimba_.cfspatch"));
    CHECK_FALSE(std::filesystem::exists(dir / "My_ marimba_.cfspatch.tmp"));
    {
        std::ofstream bad(dir / "Broken.cfspatch", std::ios::binary);
        bad << "CFS patch 1\n11 = loud\nend\n";
    }

    const auto list = preset_lib::listPresets();
    const std::size_t factory = std::size(synth::presets::kFactoryPresets);
    REQUIRE(list.size() == factory + 2);
    CHECK(list[factory].name == "Broken"); // user presets after the factory bank, alphabetical
    CHECK(list[factory + 1].name == "My_ marimba_");

    synth::Patch p;
    std::string name;
    REQUIRE(preset_lib::loadPreset(list[factory + 1], p, name, error));
    CHECK(name == "My: marimba?"); // the name inside the file, not the sanitized file name
    for (const synth::ParamInfo& info : synth::kParams)
        CHECK(synth::getParam(p, info) == synth::getParam(synth::presets::marimba(), info));
    CHECK_FALSE(preset_lib::loadPreset(list[factory], p, name, error));
    CHECK(error.find("line 2") != std::string::npos);
    REQUIRE(preset_lib::loadPreset(list[0], p, name, error)); // a factory entry
    CHECK(name == "Init");

    REQUIRE(_putenv_s("CFS_PRESET_DIR", "") == 0);
    std::filesystem::remove_all(dir);
}
