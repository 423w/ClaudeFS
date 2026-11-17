# Decisions

> **CFS note:** in this repo **Claude makes these calls**, plumbing decisions included, so the owner can compare
> Claude's choices with their own in FSS. The owner's decisions made before the fork (D2, and D1's leaning) still bind.
> Each Log entry adds one extra line: **The owner might choose differently if:** ...

Each entry lists its options, the trade-offs, and a recommendation. When a decision is made, mark it `DECIDED`
and append an entry to the **Log** at the bottom (date, decision, options considered, why, what would make you revisit).

Status legend: `OPEN` (not yet faced) · `LEANING` (tentative, revisit at the named lesson) · `DECIDED`

---

### D1. Synth type: `DECIDED` → FM-core hybrid  *(CFS: leaning in L0.4, confirmed at L1.5, see Log)*
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

### D5. Phase representation: `DECIDED` → `uint32_t` fixed point (cycles × 2^32)  *(L1.2, see Log)*
- `float` in [0,1): simple, but precision drifts for long notes and low frequencies.
- `double` in [0,1): simple and precise enough. Recommended to start.
- `uint32_t` fixed point: free wraparound (unsigned overflow is well-defined), exact, and the way the DX7 does it.
  A great C++ lesson. Consider switching once it clicks.

### D6. Envelope model: `DECIDED` → ADSR, exponential segments (one-pole with overshoot), 1 ms minimum  *(L1.3, see Log)*
- ADSR with exponential segments (standard; start here).
- DX-style 4 rates + 4 levels (more expressive for FM; authentic).
- Multi-segment (MSEG, as in Vital and Serum): most flexible, and the most UI work.
Recommendation: ADSR now, and generalize to multi-segment in Branch 4 if you want it.

### D7. Operator count & routing model: `DECIDED` → 4 ops, free 4×4 matrix, 8 preset algorithms  *(L1.5, see Log)*
- 4 ops / fixed algorithms (Digitone: 8; TX81Z: 8). 6 ops / 32 algorithms (DX7). 8 ops / free matrix (FM8).
- **Recommendation:** 4 operators, stored internally as a **free modulation matrix**, with
  **algorithm presets** that just fill in the matrix. You get easy UX and full flexibility from one data structure.

### D8. Polyphony & voice stealing: `DECIDED` → 16 voices + 4 fade slots; steal released-oldest, then held-oldest; 5 ms fade  *(L1.7, see Log; refine in L2.5)*
Voice count (8? 16? 32?), steal policy (oldest, quietest, same-key retrigger), and how fast
a stolen voice fades out (avoids clicks).

### D9. Standalone audio backend: `DECIDED` → miniaudio 0.11.23 (FetchContent, SHA-256 pinned), WASAPI shared or exclusive  *(L2.1, see Log)*
- **miniaudio** (single header, MIT-0/public domain): fastest path. Recommended.
- Raw **WASAPI**: COM, event-driven buffers, the real Windows plumbing. A great optional deep dive.
- RtAudio / PortAudio: older, solid.
- JUCE `AudioDeviceManager`: easy if you've already committed to JUCE.

### D10. MIDI input: `DECIDED` → WinMM `midiIn*` by hand  *(L2.3, see Log)*
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

### 2026-11-17: D5 decided (CFS): `uint32_t` fixed-point phase, one cycle = 2^32
**Decision.** `fss::dsp::Phase` is a `uint32_t` fraction of a cycle. `Phasor` adds a `uint32_t`
increment (`round(f/fs · 2^32)`, wrapped modulo 2^32, so negative and above-Nyquist frequencies alias
correctly) and relies on unsigned overflow for the wrap. Sines index their table with the top bits.
**Options considered.** `float` in [0,1); `double` in [0,1) (the curriculum's suggestion); `uint32_t`;
`uint64_t` (same properties as uint32, 2^32× finer).
**Measured** (one hour of samples at 48 kHz, `fss_render l1.2`):

| | 0.001 Hz | 0.1 Hz | 440 Hz |
|---|---|---|---|
| float | **stalls at phase 0.5** | +2.17 cents | −1.0 cycle drift |
| double | exact (to 1e-6 cycles) | exact | exact |
| uint32 | −9.28 cents | +0.029 cents | +0.00002 cents, +0.018 cycle drift |

**Why uint32 and not double**, even though double measured *more precise*:
(1) the wrap is free and exact, with no `floor()` and no branch;
(2) phase is a pure integer function of the sample count, so renders are bit-reproducible, which golden
files and the block-size invariance test (L6.1) need;
(3) the top bits *are* the table index (`sineTable`: shift + mask, no float→int conversion), the fastest
sine measured (0.71 ns vs 5.4 ns for `std::sin`);
(4) 32-bit lanes are twice as many per SIMD register as double (L6.2);
(5) phase modulation (L1.4) is an integer add that wraps by itself.
Its precision cost (frequency quantized to 1.1e-5 Hz) is inaudible at audio rates, constant rather
than growing, and only large *relatively* for very slow LFOs (9 cents at 0.001 Hz, a 1000-second period
off by 0.5 %).
**Revisit if:** very slow LFOs or long-term drift between oscillators ever matter (switch those to
`uint64_t`, same code shape); or through-zero FM / PM-depth conversion from float turns out costly in L1.4.
**The owner might choose differently if:** they value "phase is just a number in [0,1)" readability
over bit-exactness. `double` is simpler to reason about, and it measured more precise here. It's a
completely defensible choice, and the curriculum's recommendation.

### 2026-11-17: D6 decided (CFS): ADSR with exponential (one-pole, overshoot-target) segments
**Decision.** `fss::dsp::Adsr`: attack aims at 1.3 (RC-charging curve, reaches 1.0 in exactly attackMs);
decay and release are exponential falls aimed 0.001 past their end, with times defined as a *rate*: from
full scale, −60 dB after exactly decay/releaseMs (silent ≈ 11 % later). Sustain is a linear level. Gate-on
restarts the attack from the current level; gate-off releases from the current level in any stage. All
times are clamped to ≥ 1 ms. Double-precision state.
**Options considered.** ADSR linear segments; ADSR exponential (chosen); DX-style 4 rates + 4 levels;
multi-segment (MSEG).
**Why.** Exponential falls are straight lines in dB, which is how loudness is heard, and the overshoot
target makes segments finish in finite, exactly known time. ADSR is what every player already knows,
and four knobs per operator × 4 operators is already 16 envelope knobs. The 1 ms floor is measured:
it's where onset click energy (−46 dB above 2 kHz, sine starting at its peak) drops below a bare sine
starting at zero (−44 dB). Times verified identical at 44.1/48/96 kHz (−60 dB at 500.00 ms).
**Revisit if:** FM patches need the DX-style "attack to level 1, decay to level 2, ..." shapes for
authentic e-pianos/brass (L1.5/L4.7); or a velocity → attack-time modulation wants a different
time definition. MSEG remains the Branch 4 option.
**The owner might choose differently if:** they find "time to −60 dB" less intuitive than "time to
zero", or prefer linear attacks (which sound more "digital"/snappy). Both are one-line changes here.
A DX-style 4-rate/4-level envelope is also a very reasonable FM-first choice.

### 2026-11-17: D7 decided (CFS): 4 operators, free 4×4 modulation matrix, 8 preset algorithms
**Decision.** `kNumOps = 4`. `Algorithm` = `mod[src][dst]` amounts (diagonal = self-feedback) + per-op
carrier gains (presets: 1/N). Operators evaluate A→D each sample; forward edges use this sample's output,
back edges (feedback, cycles) read the average of the source's last two outputs. The 8 TX81Z-family
algorithms are `constexpr` data built from edge lists.
**Options considered.** 4 ops / fixed algorithms in code (switch per algorithm); 6 ops / 32 algorithms (DX7);
8 ops / free matrix (FM8); 4 ops / free matrix with presets (chosen).
**Why.** One data structure serves both the "pick an algorithm" UX and a future free matrix (L4.1), and
the evaluation rule makes *any* matrix legal (no graph sort, no forbidden cycles). At 4 operators the full
16-entry matrix loop is trivial (zero entries are skipped), so data-driven costs nothing measurable, and
tests prove the generic loop equals hand-wired chains *exactly*. 4 ops is enough for e-piano, bass, organ,
bells and brass (all rendered), and it keeps 16 envelope knobs instead of 24.
**Revisit if:** sound design keeps wanting a 5th/6th operator (DX7 patches); or profiling in L2.6/L6.2
shows the generic matrix loop matters (specialize per preset with templates then).
**The owner might choose differently if:** they prefer fixed algorithms written as code (simpler to read,
one function per algorithm), or want DX7 compatibility (6 ops, 32 algorithms, patch import).

### 2026-11-17: D1 confirmed (CFS): FM-core hybrid
**Decision.** Keep FM operators as the core; the VA filter half follows in L1.9–L1.10.
**Why.** Rendered and measured in L1.4–L1.5: bell, brass, clarinet, e-piano, FM bass, organ, feedback
sweep. All behave as theory predicts (Bessel sidebands to 0.05 dB, feedback harmonics to 0.1–0.5 dB for
β ≤ 1), and the patches cover the FM half of the target sounds with 4 operators. Nothing in Branch 1 argues
for the pivot clause. Claude can't judge "fun to program", the criterion the pivot clause actually names.
**Revisit if:** the owner, listening to the L1.5 renders, finds them unconvincing (that's the real test),
or the filter lessons show the hybrid needs a different oscillator core.
**The owner might choose differently if:** FM sound design in their own FSS felt like a fight rather
than a puzzle. Then the pivot (2–3 VA oscillators into the same filter/amp/voice structure) is right
for FSS even though CFS stays FM.

### 2026-11-17: D8 decided (CFS): 16-note polyphony, 4 steal-headroom slots, 5 ms steal fade
**Decision.** `Engine::kMaxPolyphony = 16` sounding notes in a pool of 20 voices. Over the limit, the
victim is the oldest *released* voice, else the oldest held one; it fades linearly over 5 ms *in its own
slot* while the new note starts immediately in a spare slot (if > 4 steals happen within 5 ms, the quietest
fading voice is cut: a deliberate last resort). A repeated key fades its old voice and starts a fresh one
(a re-strike) instead of restarting in place. Voices free themselves when their carriers go idle.
**Options considered.** Voice count 8 / 16 / 32. Steal oldest / quietest / released-first / same-key.
Fade schemes: hard cut (clicks); release-then-start in the same slot (delays the new note by the fade);
extra fade slots (chosen).
**Why.** 16 covers two-handed playing with sustain-pedal tails for FM sounds. Released-first steals what
the ear already expects to go away. Fade slots give no click *and* no latency. Measured (48 notes 25 ms
apart into 8 voices, pure-sine pad): worst click −51 dB with the fade vs −37.5 dB with a hard cut;
loud frames 30 vs 111. Same-key re-strike avoids a phase-reset click.
**Revisit if:** L2.6 profiling shows 20 voices is too costly (it won't be at 4 ops); a pad needs more than
16 notes (raise it); or quietest-first stealing sounds better live (L2.5 refines this, with sustain).
**The owner might choose differently if:** they implement release-then-start (simpler, a few ms of
delay is often unnoticeable), or prefer "quietest" stealing (needs a cheap per-voice level meter).

### 2026-11-17: D9 decided (CFS): miniaudio
**Decision.** miniaudio 0.11.23 (single header, public domain / MIT-0) fetched by FetchContent and pinned by
SHA-256; implementation compiled once with its own warnings off; wrapped in an RAII `AudioDevice` (pimpl, so
no miniaudio types leak) with a virtual `RenderCallback`. WASAPI shared by default, `--exclusive` for low latency.
**Options considered.** miniaudio; raw WASAPI (COM, event-driven); RtAudio/PortAudio; JUCE's AudioDeviceManager.
**Why.** One header, no build system to fight, cross-platform, and it exposes what the lesson is about
(granted rate/period, share mode, fixed vs variable callbacks). Measured on the owner's machine (Realtek,
WASAPI): shared mode forces 10 ms periods (≈ 35 ms latency); exclusive mode granted 80-frame periods
(≈ 6.3 ms), with the engine using ≤ 5 % of each deadline. Raw WASAPI would teach COM, not audio.
**Revisit if:** we need ASIO (pro interfaces), or exclusive mode misbehaves on other devices; raw WASAPI
remains the stretch goal. In a plugin (Branch 3) the host owns the device and this goes away.
**The owner might choose differently if:** they want the deep dive (raw WASAPI event-driven shared mode is
the curriculum's stretch), or they already know JUCE.

### 2026-11-17: D10 decided (CFS): WinMM by hand
**Decision.** `midiInOpen` with a callback (hosts/midi), pushing 4-byte `MidiMessage`s into an
`SpscQueue<MidiMessage, 1024>`; real-time bytes (clock, active sensing) dropped at the source; a pure,
portable classifier and a running-status byte-stream parser for everything that isn't WinMM.
**Options considered.** WinMM by hand; RtMidi; libremidi; Windows MIDI Services (Windows 11-oriented).
**Why.** It's ~150 lines, no dependency, and it teaches exactly what L2.3 is about: a callback on a thread
you don't own, raw status/data bytes, and a lock-free hand-off to the audio thread. The parsing layer is
host-independent, so the CLAP host (which also delivers raw MIDI 1.0 bytes) reuses it.
**Revisit if:** cross-platform standalone builds matter (RtMidi/libremidi), MIDI 2.0 / per-note
expression is wanted, or a device misbehaves under WinMM.
**The owner might choose differently if:** they'd rather not touch Win32 at all (libremidi is a clean
modern C++ choice), or they want macOS/Linux support for the standalone.

<!-- Append entries like:
### 2026-11-20: D5 decided: double phase in [0,1)
Why: ...
Revisit if: ...
-->
