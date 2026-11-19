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
void renderL1_11(const std::filesystem::path& outDir); // l1_11.cpp: LFO shapes, vibrato, wobble, pad, key sync
void renderL2_4(const std::filesystem::path& outDir); // l2_4.cpp: parameter smoothing A/B (zipper noise)
void renderL2_5(const std::filesystem::path& outDir); // l2_5.cpp: legato/mono/poly, glide, sustain pedal, bend
void renderL2_6(const std::filesystem::path& outDir); // l2_6.cpp: CPU per voice, hot spots, denormals (console)
void renderL3_4(const std::filesystem::path& outDir); // l3_4.cpp: factory bank files + a render of every preset
void renderL4_1(const std::filesystem::path& outDir); // l4_1.cpp: mod matrix pads, host poly-mod
void renderL4_2(const std::filesystem::path& outDir); // l4_2.cpp: operator waveforms, FM aliasing 1x/2x, VA/TX presets
void renderL4_4(const std::filesystem::path& outDir); // l4_4.cpp: unison pads, mono check, CPU
void renderL5_1(const std::filesystem::path& outDir); // l5_1.cpp: FX slot switching (clicks), D16 dispatch timing
void renderL5_2(const std::filesystem::path& outDir); // l5_2.cpp: drive, oversampling, aliasing, latency, bitcrush
void renderL5_3(const std::filesystem::path& outDir); // l5_3.cpp: delay line, synced delay, time changes, spill
void renderL5_4(const std::filesystem::path& outDir); // l5_4.cpp: chorus e-piano, flanger vs phaser, interpolation
void renderL5_5(const std::filesystem::path& outDir); // l5_5.cpp: Schroeder, Freeverb, the FDN reverb, RT60

} // namespace fss::render
