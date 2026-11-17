// engine_render.h: play a score through fss::Engine, block by block, like a real host would (L1.7).
#pragma once

#include "score.h"

#include <fss/engine.h>

#include <vector>

namespace fss::render {

struct StereoBuffer {
    std::vector<float> left, right;
};

struct EngineRenderOptions {
    int blockSize = 256;      // the host's block size; the result must not depend on it (invariance test)
    int polyphony = Engine::kMaxPolyphony;
    float stealFadeMs = 5.0f;
    double tailMs = 1500.0;   // keep rendering after the last note-off, for release tails
};

struct EngineRenderStats {
    std::uint64_t steals = 0;
    int maxBusyVoices = 0;
};

/// Turn score notes into sample-stamped events, slice them into blocks, and call Engine::process per block.
[[nodiscard]] StereoBuffer renderWithEngine(const synth::Patch& patch, const std::vector<ScoreNote>& score,
                                            const EngineRenderOptions& opt = {}, EngineRenderStats* stats = nullptr);

/// Write a stereo buffer as a float WAV and print a one-line summary.
void writeStereo(const std::filesystem::path& path, const StereoBuffer& buf);

/// Sum b into a (sizes may differ; a grows), scaled by gain.
void mixInto(StereoBuffer& a, const StereoBuffer& b, float gain = 1.0f);

} // namespace fss::render
