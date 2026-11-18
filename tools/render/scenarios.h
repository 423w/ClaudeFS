// scenarios.h: one function per lesson, each rendering that lesson's sounds into a folder.
// main.cpp maps names ("l1.1", "l1.2", ...) to these.
#pragma once

#include <filesystem>

namespace fss::render {

void renderL1_1(const std::filesystem::path& outDir); // l1_1.cpp: WAV writer checks, clipping, clicks
void renderL1_2(const std::filesystem::path& outDir); // l1_2.cpp: phasor, glides, chord, phase precision
void renderL1_3(const std::filesystem::path& outDir); // l1_3.cpp: ADSR envelopes, clicks
void renderL1_4(const std::filesystem::path& outDir); // l1_4.cpp: two-operator FM, Chowning sounds
void renderL1_5(const std::filesystem::path& outDir); // l1_5.cpp: 4-op voice, algorithms, feedback
void renderL1_6(const std::filesystem::path& outDir); // l1_6.cpp: patch, velocity, key scaling, scores
void renderL1_7(const std::filesystem::path& outDir); // l1_7.cpp: Engine, polyphony, stealing, the M2 piece
void renderL1_8(const std::filesystem::path& outDir); // l1_8.cpp: saw/square/triangle, aliasing, PolyBLEP
void renderL1_9(const std::filesystem::path& outDir); // l1_9.cpp: one-pole and biquad filters, responses, sweeps
void renderL1_10(const std::filesystem::path& outDir); // l1_10.cpp: TPT SVF vs biquad, per-voice filter
void renderL2_4(const std::filesystem::path& outDir); // l2_4.cpp: parameter smoothing A/B (zipper noise)

} // namespace fss::render
