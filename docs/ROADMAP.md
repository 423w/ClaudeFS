# CFS: Roadmap

> **CFS note:** this roadmap is copied unchanged from FirstStreetSynthesis so lesson numbers line up between
> the two repos. Here Claude builds every lesson itself (build mode), and the **Progress** checklist at the bottom
> tracks CFS, not the owner's FSS. Text below that says "you" means whoever is building: in this repo, Claude.

This is the map for the project. It's a tree of lessons organized breadth-first: the trunk, then the
big branches, then the lessons on each branch. Each lesson is one chunk of learning plus building, and
it's sized so you can take it into a fresh Claude conversation.

> **How to read this:** the *tree* is organized by topic. The *route* (further down) is the suggested
> order to walk it in. They're different on purpose: you'll get a playable synth well before you've
> finished the "engine" branch, because hearing it live is what keeps you coming back.

Companion docs:
- [ARCHITECTURE.md](ARCHITECTURE.md): the target design the lessons build toward.
- [DECISIONS.md](DECISIONS.md): every decision point, with options and recommendations. **You** make the calls.
- [RESOURCES.md](RESOURCES.md): annotated books, papers, code, and tools, referenced by ID (e.g. `[ZAV]`).
- [curriculum/](curriculum/): the lessons themselves.
- [journal/](journal/): your lab notebook.

---

## Level 0: The trunk

**FirstStreetSynthesis (FSS)** is a polyphonic **FM-core hybrid** synthesizer: phase-modulation
operators, then a virtual-analog filter, then an amp, with envelopes, LFOs, and a mod matrix, an effects
section, and a plugin you can load in your DAW. You write as much of it from scratch as you choose.

*(The synth type is decision **D1**. FM-core hybrid is the recommendation, and [L0.4](curriculum/0-orientation.md#l04-choose-your-synth-decision-gate-d1-d2)
walks you through deciding for yourself.)*

## Level 1: The big branches

| # | Branch | Question it answers | Output |
|---|--------|---------------------|--------|
| 0 | [**Orientation**](curriculum/0-orientation.md) | What *is* a synth, what math do I need, what am I building? | Toolchain, vocabulary, the D1 decision |
| 1 | [**The Offline Engine**](curriculum/1-offline-engine.md) | How do numbers become sound? | A DSP library that renders patches to `.wav` |
| 2 | [**Real-Time**](curriculum/2-realtime.md) | How do I play it live without glitches? | A standalone app you play from a MIDI keyboard |
| 3 | [**Into the DAW**](curriculum/3-plugin.md) | How does a plugin talk to a host? | FSS as a CLAP/VST3 plugin with a GUI |
| 4 | [**Sound Design Depth**](curriculum/4-sound-design.md) | How do I make it an *instrument*? | Mod matrix, wavetables, unison, drive, presets |
| 5 | [**Effects Engine**](curriculum/5-effects.md) | How are drive, delay, chorus, and reverb built? | A built-in FX chain |
| 6 | [**Craft & Mastery**](curriculum/6-craft.md) | How do pros make it fast, robust, and shippable? | Tests, SIMD, oversampling, a release |

Branch 6 is cross-cutting. You pull lessons from it when another lesson needs them (testing starts in L0.3).

## Level 2: The lessons

```
FSS
├── 0 Orientation
│   ├── L0.1  Listening & vocabulary: what a synth is
│   ├── L0.2  Just-enough math: phase, frequency, dB, exponentials, sampling
│   ├── L0.3  Toolchain & repo skeleton (CMake, tests, analysis scripts)
│   └── L0.4  Choose your synth ............................ [DECISION D1, D2]
│
├── 1 The Offline Engine
│   ├── L1.1  Digital audio & writing a WAV file from scratch
│   ├── L1.2  The phase accumulator & sine oscillator ...... [DECISION D5]
│   ├── L1.3  Envelopes: ADSR as a state machine ........... [DECISION D6]
│   ├── L1.4  Two-operator FM/PM: sidebands & Bessel functions
│   ├── L1.5  Operators, algorithms & feedback (the FM voice) [DECISION D7, confirm D1]
│   ├── L1.6  The note: pitch, velocity, key scaling
│   ├── L1.7  Voices, polyphony & the Engine ............... [DECISION D8]
│   ├── L1.8  Harmonics, classic waveforms & aliasing (PolyBLEP)
│   ├── L1.9  Filters I: one-pole, biquads, frequency response
│   ├── L1.10 Filters II: virtual analog (TPT/ZDF SVF, ladder)
│   └── L1.11 LFOs & basic modulation
│
├── 2 Real-Time
│   ├── L2.1  Audio callbacks, buffers, latency (standalone host) [DECISION D9]
│   ├── L2.2  The real-time rules (no allocs, no locks, no I/O)
│   ├── L2.3  MIDI input & your first lock-free queue ...... [DECISION D10]
│   ├── L2.4  Parameters, smoothing & a dev control surface  [DECISION D11, D12]
│   ├── L2.5  Playing it like an instrument: stealing, legato, bend, sustain
│   └── L2.6  CPU budget, profiling & denormals
│
├── 3 Into the DAW
│   ├── L3.1  What a plugin is: the host/plugin contract ... [DECISION D2 final, D13]
│   ├── L3.2  CLAP by hand
│   ├── L3.3  Reaching every DAW: VST3 via clap-wrapper or JUCE
│   ├── L3.4  State, presets & versioning .................. [DECISION D15]
│   ├── L3.5  GUI I: editor architecture & threads ......... [DECISION D14]
│   └── L3.6  GUI II: scope, your own FFT, spectrum analyzer
│
├── 4 Sound Design Depth
│   ├── L4.1  The modulation matrix & polyphonic modulation
│   ├── L4.2  Richer operators: waveforms, ratios, FM aliasing
│   ├── L4.3  Wavetable oscillators & mipmapping
│   ├── L4.4  Unison, detune & stereo
│   ├── L4.5  Nonlinearity in the voice: drive, waveshaping, ADAA
│   ├── L4.6  Detours: Karplus-Strong, additive, noise (play!)
│   └── L4.7  Sound design sprint: a factory preset bank
│
├── 5 Effects Engine
│   ├── L5.1  FX architecture: the chain, dry/wet, tails ... [DECISION D16]
│   ├── L5.2  Gain, saturation, distortion, bitcrush & oversampling
│   ├── L5.3  Delay lines & a tempo-synced delay
│   ├── L5.4  Modulated delays: chorus, flanger, phaser
│   ├── L5.5  Reverb: Schroeder → Freeverb → FDN/Dattorro ... [DECISION D17]
│   └── L5.6  EQ & dynamics: parametric EQ, compressor, output limiter
│
└── 6 Craft & Mastery (cross-cutting)
    ├── L6.1  Testing DSP: golden files, properties, null tests, CI
    ├── L6.2  Performance: data-oriented design & SIMD voices
    ├── L6.3  Oversampling & anti-aliasing filter design
    ├── L6.4  Robustness: sample rates, block sizes, NaNs, pluginval
    ├── L6.5  Shipping: versioning, licensing, installers .. [DECISION D18]
    └── L6.6  Making music with it (the real point)
```

## The route: suggested walking order

```
Phase A  "First Sound"     L0.1 → L0.2 → L0.3 → L0.4 → L1.1 → L1.2 → L1.3 → L1.4
         ★ Milestone M1: a Chowning-style FM bell and brass note, rendered to WAV, sidebands verified.

Phase B  "First Song"      L1.5 → L1.6 → L1.7   (+ L6.1 basics)
         ★ Milestone M2: a 4-op polyphonic FM engine renders a chord progression/melody offline.
         ► Confirm D1 here. If FM isn't speaking to you, pivot (see L1.5).

Phase C  "Play It"         L2.1 → L2.2 → L2.3 → L2.4
         ★ Milestone M3: you play FSS live from a MIDI keyboard with knobs (CCs) on the parameters.

Phase D  "Shape It"        L1.8 → L1.9 → L1.10 → L1.11 → L2.5 → L2.6
         ★ Milestone M4: FM → resonant filter → amp, with filter envelope and LFOs, played live, no clicks.

Phase E  "In the DAW"      L3.1 → L3.2 → L3.4 → L3.5   (+ L6.4)
         ★ Milestone M5: FSS loads in FL Studio as a CLAP, automates, saves with the project. Use it on a track.
         (L3.3, VST3, is deferred to Phase G. FL Studio loads CLAP natively. See D2.)

Phase F  "Grow It"         Branches 4 and 5 in whatever order excites you. Suggested:
                           L5.1 → L5.3 → L5.4 (chorus! FM e-pianos love it) → L4.1 → L4.4
                           → L5.2 → L5.5 → L4.2 → L4.3 → L4.5 → L3.6 → L5.6 → L4.7
         ★ Milestone M6: an instrument you reach for over presets in other synths.

Phase G  "Master It"       L3.3 (VST3 for every DAW), L6.2, L6.3, L6.5, L4.6 detours; then add features
                           from your own music-making.
```

Rough sizing: each lesson is somewhere between one long session and a few weeks of evenings. There's
no schedule. Go where curiosity pulls; the route is a suggestion. The rule is **don't skip exit
criteria**, because they're how you know the foundation underneath you is solid.

---

## How to run a lesson with Claude

1. Start a new conversation in this repo. Say something like:
   > "Let's start **L1.4**. Read CLAUDE.md, docs/ROADMAP.md, and the L1.4 section of
   > docs/curriculum/1-offline-engine.md. I want to be in **teach mode**."
2. Claude reads [CLAUDE.md](../CLAUDE.md), which sets the tutoring rules. Then:
   - **Concepts first.** Read and discuss the primary resources. Ask "why" until it clicks.
   - **Predict, then build, then measure.** Before running anything, predict what it will sound like or
     what the spectrum will show. Then build it and render it. Then check it with the analysis tools.
   - **Hit the exit criteria.** Claude checks them with you.
3. When you hit a **[DECISION]**, Claude lays out options and gives a recommendation. You choose, and
   the choice gets recorded in [DECISIONS.md](DECISIONS.md).
4. Close the lesson: tick it below, and write a short journal entry ([journal/TEMPLATE.md](journal/TEMPLATE.md)).

Modes (tell Claude which one you want):
- **teach**: you write the code, and Claude explains, hints, and reviews. *(This is the default.)*
- **pair**: Claude writes the scaffolding (CMake, host glue, test harness), and you write the DSP and synth logic.
- **build**: Claude implements, then walks you through every non-obvious line and suggests experiments.

---

## Progress

### Branch 0: Orientation
- [x] L0.1 Listening & vocabulary
- [x] L0.2 Just-enough math
- [x] L0.3 Toolchain & repo skeleton
- [x] L0.4 Choose your synth (D1, D2)

### Branch 1: The Offline Engine
- [x] L1.1 Digital audio & WAV
- [x] L1.2 Phase accumulator & sine
- [x] L1.3 Envelopes
- [x] L1.4 Two-operator FM/PM ★M1
- [x] L1.5 Operators, algorithms & feedback
- [x] L1.6 The note
- [x] L1.7 Voices, polyphony & the Engine ★M2
- [x] L1.8 Waveforms & aliasing
- [x] L1.9 Filters I
- [x] L1.10 Filters II
- [x] L1.11 LFOs & basic modulation

### Branch 2: Real-Time
- [x] L2.1 Audio callbacks & latency
- [x] L2.2 The real-time rules
- [x] L2.3 MIDI & lock-free queue
- [x] L2.4 Parameters & smoothing ★M3
- [x] L2.5 Playing it like an instrument
- [x] L2.6 CPU budget & denormals ★M4

### Branch 3: Into the DAW
- [x] L3.1 The host/plugin contract
- [x] L3.2 CLAP by hand
- [ ] L3.3 VST3 / every DAW
- [x] L3.4 State & presets
- [x] L3.5 GUI I ★M5 (everything headless passes; the FL Studio run is the owner's: docs/FL-STUDIO.md)
- [ ] L3.6 GUI II: FFT & analyzer

### Branch 4: Sound Design Depth
- [x] L4.1 Mod matrix (L1.11 routes = fixed slots, golden test; CLAP poly-mod on cutoff/resonance; FL check: docs/FL-STUDIO.md section 7)
- [ ] L4.2 Richer operators
- [ ] L4.3 Wavetables
- [x] L4.4 Unison & stereo (FM-core copies, "Wide pad"; supersaw waits for L4.2; FL check: docs/FL-STUDIO.md section 7)
- [ ] L4.5 Drive & ADAA
- [ ] L4.6 Detours
- [ ] L4.7 Preset bank ★M6

### Branch 5: Effects
- [x] L5.1 FX architecture (D16; FL check: docs/FL-STUDIO.md section 7)
- [x] L5.2 Distortion & oversampling (drive 1x/2x/4x + crush; clap.latency with restart; FL check: docs/FL-STUDIO.md section 7)
- [x] L5.3 Delay (tempo sync from the CLAP transport; FL check: docs/FL-STUDIO.md section 7)
- [x] L5.4 Chorus/flanger/phaser ("E-piano chorus" preset; FL check: docs/FL-STUDIO.md section 7)
- [ ] L5.5 Reverb
- [ ] L5.6 EQ & dynamics

### Branch 6: Craft
- [ ] L6.1 Testing DSP
- [ ] L6.2 Performance & SIMD
- [ ] L6.3 Oversampling deep dive
- [ ] L6.4 Robustness (Phase E slice in L3.2/L3.4: NaN guard, fuzzing, lifecycle, rates, block sizes; pluginval waits for VST3)
- [ ] L6.5 Shipping
- [ ] L6.6 Making music (never actually done)
