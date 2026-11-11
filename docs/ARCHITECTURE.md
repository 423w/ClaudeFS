# FirstStreetSynthesis: Target Architecture

This is where the lessons are heading. **Don't build this all at once.** Each lesson adds one piece,
and early lessons use deliberately simple versions. The design changes as you learn. When you change a
principle below, record why in [DECISIONS.md](DECISIONS.md).

---

## 1. The signal path (what you hear)

```
                       ┌──────────────── one Voice (×N, polyphonic) ────────────────┐
 MIDI note ──► Note ──►│  Operators (FM/PM graph)  ──►  Filter (VA SVF)  ──►  Amp   │──┐
 velocity       │      │   op1 op2 op3 op4               cutoff/res          VCA    │  │
                │      │   ↑ each has its own            ↑                   ↑      │  │  Σ voices
                │      │     envelope + ratio            filter env          amp env│  │
                │      └───────────────▲────────────────────▲──────────────────▲────┘  │
                │                      └──── Mod sources: Envs, LFOs, velocity, ┘       │
                └────────────────────────── keytrack, wheel, aftertouch, random         ▼
                                                                         ┌──────────────────────┐
                                                                         │ FX chain (global)    │
                                                                         │ drive→chorus→delay→  │──► out L/R
                                                                         │ reverb→EQ→limiter    │
                                                                         └──────────────────────┘
```

- **Operators** are oscillators (sine first, more waveforms later) with their own envelope and level.
  They can modulate each other's *phase*. The routing between them is the **algorithm**.
- **Filter** is per voice, so every note gets its own filter envelope. This is the "subtractive" half of the hybrid.
- **Amp** is the final gain stage, shaped by the amp envelope and velocity.
- **FX** is global (stereo, after the voices are summed).

## 2. The code layers (what you write)

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ HOSTS (thin adapters, the only code that knows about the outside world)       │
│  tools/render     offline CLI: note list + patch → .wav     (Branch 1)       │
│  hosts/standalone audio device + MIDI in + dev UI           (Branch 2)       │
│  hosts/clap       hand-written CLAP plugin                  (Branch 3)       │
│  hosts/juce       optional JUCE wrapper (VST3/AU/standalone) (Branch 3)      │
└──────────────────────────────┬───────────────────────────────────────────────┘
                               │  Engine API: prepare / reset / process(events) / params / state
┌──────────────────────────────▼───────────────────────────────────────────────┐
│ fss_core   (static library, C++20, depends only on the standard library)      │
│                                                                              │
│  fss::synth   Engine ─► VoiceAllocator ─► Voice[N]                           │
│               Voice = Operator[4] ─► Svf ─► Amp,  ModSources, ModMatrix      │
│               Patch / ParamTable / Algorithm tables                          │
│  fss::fx      FxChain: Drive, Chorus, Delay, Reverb, Eq, Limiter             │
│  fss::dsp     Phasor, Sine, PolyBlep, Adsr, OnePole, Biquad, Svf, Ladder,     │
│               DelayLine, Smoother, Lfo, Wavetable, Oversampler               │
│  fss::util    math (dB, pitch), SpscQueue, DenormalGuard, units               │
└──────────────────────────────────────────────────────────────────────────────┘
        ▲
        │ tests/ (Catch2 or doctest): unit, golden-file, property tests
        │ tools/analyze/ (Python): spectrum, spectrogram, waveform, filter response plots
```

### Proposed folder layout
```
FirstStreetSynthesis/
├── CMakeLists.txt
├── CMakePresets.json
├── core/
│   ├── CMakeLists.txt
│   ├── include/fss/{util,dsp,synth,fx}/*.h
│   └── src/...                       (only where a .cpp is really needed)
├── tools/
│   ├── render/                       C++ CLI: offline renderer
│   └── analyze/                      Python: plot_spectrum.py, spectrogram.py, ...
├── hosts/{standalone,clap,juce}/
├── tests/
├── renders/                          (git-ignored) WAV and plot output
└── docs/
```

## 3. Design principles (the non-negotiables, until you decide otherwise)

1. **The core is pure.** `fss_core` has no dependencies beyond the standard library and knows nothing
   about devices, MIDI APIs, plugin formats, or GUIs. Hosts translate the outside world into Engine calls.
   *Why:* the same DSP runs in the offline renderer, the standalone app, the CLAP plugin, JUCE, and the tests.
2. **Everything is renderable offline.** If you can't render it to a WAV deterministically, you can't
   test it, plot it, or compare it. Offline-first also means you learn DSP before you fight real-time.
3. **`prepare()` allocates; `process()` never does.** No `new`/`malloc`, no `std::vector::push_back`,
   no locks, no file or console I/O, and no exceptions inside `process()`. (Lesson L2.2 explains why.)
4. **Primitives are small value types** with a uniform shape:
   ```cpp
   void prepare(float sampleRate);   // compute rate-dependent constants
   void reset();                      // return to silence / initial state
   float tick(float in);              // one sample (generators: tick())
   void process(float* buf, int n);   // optional block version, added when profiling says so
   ```
5. **Parameters are data.** One table defines every parameter (id, name, range, default, skew,
   unit, smoothing). Host params, UI, presets, and MIDI CC mapping are all generated from it.
6. **Events are sample-accurate.** `process()` receives a time-stamped event list and splits the block
   at each event's offset. (CLAP gives you exactly this.)
7. **Per-sample clarity first, speed later.** Write the obvious per-sample code, measure it, then
   optimize (block processing, SIMD) in Branch 6, with tests guarding behavior.
8. **Floats for audio.** `float` for samples. Use `double` (or fixed point) only where precision
   demonstrably matters (phase accumulators, some filter coefficients). See D5.

## 4. Threads (Branch 2 onward)

```
  Main / UI thread                         Audio thread (real-time, high priority)
  ───────────────                          ─────────────────────────────────────
  GUI knobs, preset load,      ──SPSC──►   Engine::process(events, out, n)
  MIDI from device callback    queue /     - drain param + MIDI events
  host automation (via host)   atomics     - render voices, FX
                               ◄──SPSC──   - push meter/scope data back
  meters, scope, voice count     queue
```
- Exactly one thread runs `process()`. Everything else talks to it through **lock-free single-producer
  single-consumer queues** or **atomics**. You'll write the queue yourself in L2.3.
- In a plugin, the host owns the threads. The same rules apply: the host calls `process` on its audio
  thread and other callbacks on its main thread. CLAP documents which thread each call comes from.

## 5. Core interfaces (a sketch, not a spec)

```cpp
namespace fss {

// ---- events -------------------------------------------------------------
struct Event {
    enum class Type : std::uint8_t { NoteOn, NoteOff, ParamChange, PitchBend, Controller, Sustain };
    Type          type;
    std::uint32_t frame;      // offset within the current block
    std::int32_t  noteId;     // host note id (CLAP) or -1
    std::uint8_t  key;        // MIDI key 0..127
    std::uint8_t  channel;
    std::uint32_t paramId;
    float         value;      // velocity 0..1, param value, bend -1..1, etc.
};

// ---- parameters ---------------------------------------------------------
enum class Skew : std::uint8_t { Linear, Log, Exponential, Stepped };
struct ParamInfo {
    std::uint32_t    id;            // stable forever (presets depend on it!)
    std::string_view name, unit;
    float            min, max, def;
    Skew             skew;
    float            smoothingMs;
};
// constexpr std::array<ParamInfo, kNumParams> kParams = { ... };   // the single source of truth

// ---- engine -------------------------------------------------------------
class Engine {
public:
    void prepare(double sampleRate, int maxBlockSize);   // may allocate
    void reset();                                        // RT-safe
    void process(std::span<const Event> events,
                 float* outL, float* outR, int numFrames); // RT-safe, sample-accurate
    // state (main thread)
    std::vector<std::byte> saveState() const;
    bool loadState(std::span<const std::byte>);
};

// ---- FM voice building blocks (Branch 1) --------------------------------
constexpr int kNumOps = 4;                     // D7
struct Algorithm {                             // data, not code
    std::array<std::array<float, kNumOps>, kNumOps> mod{};  // mod[src][dst] depth (0 = no route)
    std::array<float, kNumOps> carrierMix{};               // how much each op goes to output
};
// Preset algorithms are just constexpr matrices; a "free matrix" UI edits the same struct.

} // namespace fss
```

## 6. What we borrow vs. write

| Area | Default | Rationale |
|------|---------|-----------|
| All DSP (oscillators, envelopes, filters, FX, FFT) | **Write it** | This is the point of the project |
| WAV writer | **Write it** (L1.1) | Small, and teaches binary formats |
| Lock-free SPSC queue | **Write it** (L2.3) | Core concurrency lesson. Compare against moodycamel's queue afterward |
| Audio device I/O | `miniaudio` → optionally raw WASAPI | D9. Device APIs are a deep pit; take the plunge only if you want to |
| MIDI input | WinMM `midiIn*` (by hand) or RtMidi/libremidi | D10 |
| Plugin API | **CLAP by hand**, VST3 via `clap-wrapper` (or JUCE) | D13 |
| GUI | Your choice in L3.5 | D14 |
| Tests | Catch2 v3 or doctest | D4 |
| Analysis plots | Python + numpy/scipy/matplotlib | Fast iteration. Not shipped |

Reading other synths' source (Dexed, Surge XT, Vital, Mutable Instruments) is encouraged. *Copying*
code has license consequences (see D18).
