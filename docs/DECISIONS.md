# Decisions

> **CFS note:** in this repo **Claude makes these calls**, plumbing decisions included, so the owner can compare
> Claude's choices with their own in FSS. The owner's decisions made before the fork (D2, and D1's leaning) still bind.
> Each Log entry adds one extra line: **The owner might choose differently if:** ...

Each entry lists its options, the trade-offs, and a recommendation. When a decision is made, mark it `DECIDED`
and append an entry to the **Log** at the bottom (date, decision, options considered, why, what would make you revisit).

Status legend: `OPEN` (not yet faced) · `LEANING` (tentative, revisit at the named lesson) · `DECIDED`

---

### D1. Synth type: `LEANING` → FM-core hybrid  *(CFS L0.4 confirmed the leaning, see Log; confirm at end of L1.5)*
| Option | Learning payoff | Sound | Risk |
|---|---|---|---|
| Subtractive / VA | Anti-aliased oscillators, filter design (deep DSP) | Classic analog, intuitive | "Sounds like everything else" |
| **FM / PM** | Spectrum math (sidebands, Bessel), graph routing, envelopes everywhere | Bells, e-pianos, basses, metallic, glassy | Unintuitive to program; aliasing at high index |
| Additive | Fourier made literal | Organs, evolving spectra | Hundreds of parameters; CPU |
| Wavetable | Interpolation, mipmapping, FFT | Modern, EDM | Needs tables/content |
| Physical modeling | Delay lines, feedback, physics | Plucks, realism | Narrow palette per model |
| Granular | Windows, scheduling | Textures | Sample-based; less a "synth engine" |
| **Hybrid** | Combines the above | Wide | Scope creep |

**Recommendation: FM-core hybrid.** That's 4-op phase modulation into a per-voice VA filter (like Ableton
Operator, Elektron Digitone, or Korg opsix). FM starts with sines, so you hear results on day one without fighting
aliasing. Every operator needs an envelope, so envelopes become central instead of an afterthought. The filter
half then teaches the deepest DSP topic, filters, and makes the synth easier to program. Later lessons
add PolyBLEP and wavetable waveforms to the operators, so the hybrid grows naturally.
**Pivot clause:** if FM doesn't speak to you by the end of L1.5, swap the operator stack for 2–3
VA oscillators. Every later lesson still applies.

### D2. Target DAW(s) & plugin format: `DECIDED` → FL Studio, hand-written CLAP first; VST3 near the end  *(see Log)*
Which DAW you use most decides the path:
- **Bitwig, REAPER, FL Studio (2024+), Studio One 7+** load **CLAP** natively, so the hand-written CLAP plugin works directly.
- **Ableton Live** (no CLAP as of late 2025/2026) needs **VST3** (or AU on Mac), via `clap-wrapper` or JUCE.
- **Pro Tools** needs AAX (avoid; heavy licensing).
Recommendation: write CLAP by hand (most educational), and add VST3 via `clap-wrapper` if your DAW needs it.

### D3. Build toolchain: `DECIDED` → CMake + Ninja + MSVC (VS 2022's own copies), C++20  *(L0.3, see Log)*
Alternatives: clang-cl (better warnings, and sanitizers), Visual Studio IDE vs VS Code + clangd.
Make sure it's CMake: every audio library and plugin SDK speaks it.

### D4. Test framework: `DECIDED` → Catch2 v3 via FetchContent, pinned by hash  *(L0.3, see Log)*
Catch2 v3 (built-in micro-benchmarks, rich matchers) vs doctest (tiny, very fast compile).
Either is fine; pick one and move on.

### D5. Phase representation: `OPEN`  *(L1.2)*
- `float` in [0,1): simple, but precision drifts for long notes and low frequencies.
- `double` in [0,1): simple and precise enough. Recommended to start.
- `uint32_t` fixed point: free wraparound (unsigned overflow is well-defined), exact, and the way the DX7 does it.
  A great C++ lesson. Consider switching once it clicks.

### D6. Envelope model: `OPEN`  *(L1.3)*
- ADSR with exponential segments (standard; start here).
- DX-style 4 rates + 4 levels (more expressive for FM; authentic).
- Multi-segment (MSEG, as in Vital and Serum): most flexible, and the most UI work.
Recommendation: ADSR now, and generalize to multi-segment in Branch 4 if you want it.

### D7. Operator count & routing model: `OPEN`  *(L1.5)*
- 4 ops / fixed algorithms (Digitone: 8; TX81Z: 8). 6 ops / 32 algorithms (DX7). 8 ops / free matrix (FM8).
- **Recommendation:** 4 operators, stored internally as a **free modulation matrix**, with
  **algorithm presets** that just fill in the matrix. You get easy UX and full flexibility from one data structure.

### D8. Polyphony & voice stealing: `OPEN`  *(L1.7, refined L2.5)*
Voice count (8? 16? 32?), steal policy (oldest, quietest, same-key retrigger), and how fast
a stolen voice fades out (avoids clicks).

### D9. Standalone audio backend: `OPEN`  *(L2.1)*
- **miniaudio** (single header, MIT-0/public domain): fastest path. Recommended.
- Raw **WASAPI**: COM, event-driven buffers, the real Windows plumbing. A great optional deep dive.
- RtAudio / PortAudio: older, solid.
- JUCE `AudioDeviceManager`: easy if you've already committed to JUCE.

### D10. MIDI input: `OPEN`  *(L2.3)*
WinMM `midiInOpen` by hand (simple C API on Windows 10; teaches callbacks and raw MIDI bytes) vs
RtMidi / libremidi (cross-platform). Recommendation: WinMM by hand. It's small.

### D11. Parameter communication: `OPEN`  *(L2.4)*
`std::atomic<float>` per parameter (simple; lossy for fast changes) vs an event queue (sample-accurate;
what CLAP does anyway) vs both. Recommendation: event queue as the canonical path, since it matches CLAP.

### D12. Dev control surface before the plugin GUI: `OPEN`  *(L2.4)*
MIDI CC knobs only / Dear ImGui window / terminal UI. Dear ImGui is quick and lets you build scopes early.

### D13. Plugin framework: `OPEN`  *(L3.1)*
| Option | You learn | You give up |
|---|---|---|
| **Hand-written CLAP** (+ `clap-wrapper` for VST3) | The real host/plugin contract, the C ABI, threading | GUI and wrappers are more work |
| JUCE 8 | A huge, productive framework (GUI, VST3, AU, standalone) | Abstraction hides the plumbing. AGPLv3 or JUCE licence (Starter is free under a revenue cap) |
| Raw VST3 SDK (MIT since 3.8) | VST3's COM-like model | Verbose. Less educational per hour than CLAP |
| iPlug2 / DPF | Lighter frameworks | Smaller communities |
Recommendation: CLAP by hand first (L3.2). Use JUCE later if you want its GUI toolkit. Because the core is
host-agnostic, both can coexist.

### D14. GUI technology: `OPEN`  *(L3.5)*
JUCE components / Dear ImGui embedded in the host window / VSTGUI / a WebView UI (HTML/JS) /
hand-rolled with a 2D library. Trade-offs: control vs. effort vs. how pretty it gets.

### D15. Preset / state format: `OPEN`  *(L3.4)*
Binary blob (compact, needs careful versioning) vs JSON/text (diffable, human-editable; needs a
parser, written yourself or a single-header library). Either way: stable param IDs and a version number.

### D16. FX architecture: `OPEN`  *(L5.1)*
Fixed chain order vs reorderable slots; virtual `Effect` interface vs `std::variant` + `std::visit`.

### D17. Reverb algorithm: `OPEN`  *(L5.5)*
Freeverb (classic, simple) → Dattorro plate (beautiful, fixed topology) → FDN (scalable, modern).
Recommendation: build Freeverb to learn, then pick FDN or Dattorro as the "real" one.

### D18. License & borrowing policy: `OPEN`  *(L6.5, but think about it early)*
If you copy GPL code (Dexed, Surge XT, Vital), FSS must be GPL. MIT, Apache, or BSD code (msfa, Mutable
Instruments, Signalsmith DSP, CLAP, VST3 3.8+, miniaudio) is fine with any license as long as you keep
the notices. JUCE: AGPLv3 or a JUCE licence. *Reading* any of it to learn is fine. (This isn't legal advice.)

---

## Log

### 2026-11-11: D1 leaning confirmed in intent: FM *with filters*
The README's "FM synthesizer" always meant FM operators plus a filter section (the hybrid), not pure DX-style FM.
Status stays `LEANING` until the hands-on check at the end of L1.5.
Revisit if: FM sound design isn't fun by L1.5.

### 2026-11-11: D2 decided: FL Studio → CLAP by hand; VST3 deferred
Why: FL Studio (2024.1+) loads CLAP natively, so the hand-written CLAP plugin (L3.2) runs in the main DAW with no wrapper.
VST3 (via clap-wrapper or JUCE, L3.3) moves toward the end of the project. A MIDI keyboard is available for Branch 2 onward.
Revisit if: the main DAW changes, or a collaborator needs VST3 earlier.

### 2026-11-17: D3 decided (CFS): CMake + Ninja + MSVC, from the VS 2022 install already on the machine
**Decision.** CMake 3.31 and Ninja 1.12 as bundled with Visual Studio Community 2022 (17.14), the MSVC 19.44
compiler (`cl`), C++20. `/W4 /permissive- /Zc:__cplusplus /utf-8 /WX` on CFS's own targets only (an
`fss::warnings` INTERFACE target). Presets: `debug`, `release` (true `Release`, not `RelWithDebInfo`),
plus a `release` workflow preset. `tools/devshell.ps1` loads the VS environment into PowerShell.
**Options considered.** (a) MSVC + Ninja (chosen); (b) clang-cl + Ninja (better diagnostics, `-Wconversion`
family, same ABI); (c) the Visual Studio generator (no environment setup needed, but multi-config,
slower, and `cmake` still isn't on PATH without a dev shell); (d) installing standalone CMake/Ninja.
**Why.** Everything needed was already installed, so zero installs. MSVC is what the owner's FSS
targets and what most Windows plugin users' hosts are built with, and Ninja is fast and single-config,
which keeps Debug and Release in separate folders (`build/debug`, `build/release`). The bundled CMake
is new enough (3.31) for presets v6 and workflow presets.
**Revisit if:** we want sanitizers beyond MSVC's ASan, or clearer template errors (try clang-cl via a
`clang` preset; it needs the VS "C++ Clang tools" component); or CI on Linux/macOS (L6.1).
**The owner might choose differently if:** they prefer working inside the Visual Studio IDE (the VS
generator, or VS's "Open Folder" with these presets, which also works), want clang-cl's warnings from
day one, or installed the standalone Build Tools + CMake as the curriculum suggests. Then `cmake` is on
PATH globally and no dev-shell script is needed.

### 2026-11-17: D4 decided (CFS): Catch2 v3 (3.8.1), via FetchContent, pinned by URL + SHA-256
**Decision.** Catch2 v3.8.1 downloaded at configure time from its GitHub release tarball, verified
by `URL_HASH SHA256=18b3f70a…`, linked as `Catch2::Catch2WithMain`; `catch_discover_tests` registers
each `TEST_CASE` with CTest. (The owner approved the download in chat on 2026-11-17.)
**Options considered.** Catch2 v3 (chosen); doctest (single header, compiles much faster); a
hand-rolled micro-harness (no dependency at all); GoogleTest (heavier, less idiomatic for value tests).
**Why.** Three Catch2 features will earn their keep in a DSP project:
`GENERATE` for property tests over many inputs (the block-size invariance test, sample-rate sweeps);
floating-point matchers (`WithinRel`, `WithinAbs`, `WithinULP`), which say *how* close is close
enough; and `BENCHMARK` for the CPU work in L2.6/L6.2. The cost is a one-time ~110-step build of
Catch2 itself (≈ 10–15 s here); after that, test rebuilds are just our files.
**Why the hash.** A tag can be moved and a download can be corrupted. With the hash, a different
tarball fails the configure loudly instead of quietly changing what we test with. (Trust-on-first-use:
the hash was taken from the first HTTPS download.)
**Revisit if:** test compile times start to hurt (doctest is a drop-in-ish switch), or offline/CI
builds can't reach GitHub (vendor the tarball or use a local mirror via `FETCHCONTENT_SOURCE_DIR_CATCH2`).
**The owner might choose differently if:** they value fast compiles and zero network access over
features. doctest, vendored as one header, is a perfectly good answer for this project, and its
`doctest::Approx` covers most float checks.

### 2026-11-17: D1 leaning confirmed in CFS (L0.4): FM-core hybrid, still `LEANING`
**Decision.** Keep FM-core hybrid: 4-op phase modulation → per-voice VA filter → amp. Status stays
`LEANING` until the end-of-L1.5 check.
**Options considered.** Pure FM (DX-style), subtractive/VA, wavetable-first, FM-core hybrid; additive,
physical modeling and granular only as detours (full table above).
**Why.** Evidence instead of taste (Claude can't listen): a numpy 2-op sketch (`tools/analyze/fm2op_sketch.py`)
matched Chowning/Bessel predictions to 0.01 dB, confirming the two properties that drive the choice.
(1) FM gives harmonic *or* inharmonic spectra from the ratio alone (1:1.41 → no common fundamental),
which subtractive can't do. (2) FM's brightness control is non-monotonic (the carrier vanishes at index
2.405), which is exactly what makes pure FM hard to program, and what a filter after the operators
fixes. FM also starts from sines, so Branch 1 produces clean sound before any anti-aliasing work, while
the filter half still teaches L1.9–L1.10.
**Revisit if:** by L1.5 the 4-op voice can't produce usable everyday sounds (bass, pad, keys) with the
filter's help; or operator-envelope count makes the UI unmanageable in L3.5.
**The owner might choose differently if:** they find FM tweaking frustrating rather than intriguing in
Dexed, or their music leans on warm analog pads and acid lines more than keys and bells. Then the pivot
clause (2–3 VA oscillators instead of the operator stack) is the right call. Taste is the input Claude
doesn't have.

### 2026-11-17: D2 recorded for CFS: unchanged
CFS inherits the owner's D2 as-is: FL Studio, hand-written CLAP first (L3.2), VST3 via clap-wrapper near
the end (L3.3). Nothing in L0.4 argued otherwise.
**The owner might choose differently if:** n/a. This is the owner's decision; CFS follows it.

<!-- Append entries like:
### 2026-11-20: D5 decided: double phase in [0,1)
Why: ...
Revisit if: ...
-->
