// l3_4.cpp: the factory bank as files, and a listening render of every preset (L3.4).
//
//   fss_render l3.4
//
// 1. Writes presets/factory/NN <name>.cfspatch from the code presets (synth::presets::kFactoryPresets), the
//    files the plugin and fss_play embed (core/CMakeLists.txt). Re-run CMake afterwards to re-embed them.
// 2. Renders each preset playing a short phrase that suits it, to renders/l3.4/<NN name>.wav, and prints
//    peak and RMS level, so the bank can be levelled by measurement (Claude can't listen).
// 3. Times patchToText / patchFromText, the cost of a save or a preset load.
#include "engine_render.h"
#include "scenarios.h"

#include <fss/synth/params.h>
#include <fss/synth/patch_io.h>
#include <fss/synth/presets.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace fss::render {

namespace {

std::vector<ScoreNote> phraseFor(std::string_view name)
{
    std::vector<ScoreNote> n;
    if (name.find("bass") != std::string_view::npos || name == "Reso pluck") {
        // A bass line around C2, with one overlapping pair (legato/glide presets glide there).
        const int keys[] = {36, 36, 43, 48, 46, 43, 39, 41};
        for (int i = 0; i < 8; ++i)
            n.push_back({i * 300.0, static_cast<std::uint8_t>(keys[i]), static_cast<std::uint8_t>(90 + (i % 3) * 12),
                         i == 3 ? 400.0 : 250.0});
    } else if (name == "FM pad" || name == "Organ" || name == "Brass") {
        // Two chords, C major then F major over C, long enough for slow attacks.
        for (int k : {48, 60, 64, 67})
            n.push_back({0.0, static_cast<std::uint8_t>(k), 100, 1800.0});
        for (int k : {48, 60, 65, 69})
            n.push_back({2000.0, static_cast<std::uint8_t>(k), 100, 1800.0});
    } else {
        // Keys, bells, mallets, init: a rising arpeggio at varying velocity, then a chord.
        const int keys[] = {60, 64, 67, 72, 76, 79, 84};
        for (int i = 0; i < 7; ++i)
            n.push_back({i * 220.0, static_cast<std::uint8_t>(keys[i]), static_cast<std::uint8_t>(60 + i * 10), 180.0});
        for (int k : {48, 55, 64, 71})
            n.push_back({1700.0, static_cast<std::uint8_t>(k), 100, 1200.0});
    }
    return n;
}

double db(double x) { return x > 0.0 ? 20.0 * std::log10(x) : -999.0; }

} // namespace

void renderL3_4(const std::filesystem::path& outDir)
{
    const std::filesystem::path presetDir = FSS_PRESETS_DIR;
    std::filesystem::create_directories(presetDir);
    std::filesystem::create_directories(outDir / "l3.4");

    std::printf("Factory bank -> %s\n", presetDir.string().c_str());
    std::printf("  %-14s %8s %8s %8s  %s\n", "preset", "peak dB", "RMS dB", "bytes", "round trip");
    int index = 0;
    for (const synth::presets::NamedPreset& pr : synth::presets::kFactoryPresets) {
        const synth::Patch patch = pr.make();
        const std::string name = pr.name;
        // Init first ("00"), then the others in list order.
        const int number = name == "Init" ? 0 : ++index;
        char prefix[8];
        std::snprintf(prefix, sizeof prefix, "%02d ", number);
        const std::string text = synth::patchToText(patch, name);
        {
            std::ofstream f(presetDir / (prefix + name + ".cfspatch"), std::ios::binary); // binary: keep LF
            f << text;
        }
        // The file must read back as exactly this patch, parameter by parameter.
        const synth::PatchLoad back = synth::patchFromText(text);
        bool same = back.ok && back.name == name;
        for (const synth::ParamInfo& p : synth::kParams)
            same = same && synth::getParam(back.patch, p) == synth::getParam(patch, p);

        const StereoBuffer buf = renderWithEngine(patch, phraseFor(name), {.tailMs = 2500.0});
        double peak = 0.0, sum = 0.0;
        for (float x : buf.left) {
            peak = std::max(peak, static_cast<double>(std::abs(x)));
            sum += static_cast<double>(x) * x;
        }
        const double rms = std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, buf.left.size())));
        std::printf("  %-14s %8.1f %8.1f %8zu  %s\n", name.c_str(), db(peak), db(rms), text.size(), same ? "exact" : "MISMATCH");
        writeStereo(outDir / "l3.4" / (prefix + name + ".wav"), buf);
    }

    // What a save or a preset load costs (main thread, so it only has to be "instant" for a person).
    const synth::Patch pad = synth::presets::evolvingPad();
    using clock = std::chrono::steady_clock;
    const int reps = 2000;
    auto t0 = clock::now();
    std::size_t bytes = 0;
    for (int i = 0; i < reps; ++i)
        bytes += synth::patchToText(pad, "FM pad").size();
    const double saveUs = std::chrono::duration<double, std::micro>(clock::now() - t0).count() / reps;
    const std::string text = synth::patchToText(pad, "FM pad");
    t0 = clock::now();
    int ok = 0;
    for (int i = 0; i < reps; ++i)
        ok += synth::patchFromText(text).ok ? 1 : 0;
    const double loadUs = std::chrono::duration<double, std::micro>(clock::now() - t0).count() / reps;
    std::printf("patchToText %.1f us, patchFromText %.1f us per patch (%zu bytes; %d/%d loads ok)\n", saveUs, loadUs,
                bytes / reps, ok, reps);
    std::printf("Re-run CMake (cmake --preset release) to embed changed preset files.\n");
}

} // namespace fss::render
