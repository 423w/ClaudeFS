# Decisions

> **CFS note:** in this repo **Claude makes these calls**, plumbing decisions included, so the owner can compare
> Claude's choices with their own in FSS. The owner's decisions made before the fork (D2, and D1's leaning) still bind.
> Each Log entry adds one extra line: **The owner might choose differently if:** ...

Each entry lists its options, the trade-offs, and a recommendation. When a decision is made, mark it `DECIDED`
and append an entry to the **Log** at the bottom (date, decision, options considered, why, what would make you revisit).

Status legend: `OPEN` (not yet faced) · `LEANING` (tentative, revisit at the named lesson) · `DECIDED`

---

### D1. Synth type: `LEANING` → FM-core hybrid  *(L0.4, confirm at end of L1.5)*
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

### D3. Build toolchain: `LEANING` → CMake + Ninja + MSVC (VS 2022 Build Tools), C++20  *(L0.3)*
Alternatives: clang-cl (better warnings, and sanitizers), Visual Studio IDE vs VS Code + clangd.
Make sure it's CMake: every audio library and plugin SDK speaks it.

### D4. Test framework: `OPEN`  *(L0.3)*
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

<!-- Append entries like:
### 2026-11-20: D5 decided: double phase in [0,1)
Why: ...
Revisit if: ...
-->
