# Branch 3: Into the DAW

**Branch goal:** FSS as a real plugin in your DAW. It plays notes, automates parameters, saves with the project, and has an
editor. You'll write the plugin layer by hand against the CLAP C API (the most educational path),
then reach VST3-only DAWs through a wrapper or JUCE.

**Recall the architecture:** `hosts/clap` is a *thin adapter*. If you find yourself writing DSP in
the plugin folder, stop: it belongs in `fss_core`.

---

## L3.1: What a plugin is: the host/plugin contract

**Why.** Every plugin format solves the same problems with different syntax. Understand the problems
first, and every format becomes readable.

**Concepts**
- A plugin is a **shared library** (a DLL on Windows, renamed `.clap` or `.vst3`) exporting one entry symbol.
  The host loads it, asks a **factory** for plugin instances, and talks to each instance through tables of
  function pointers (CLAP) or COM-style interfaces (VST3).
- **Lifecycle:** create → init → *activate(sampleRate, min/max block size)* (allocate here!) → start processing →
  `process()` repeatedly → stop → deactivate → destroy. Activation can happen many times (sample-rate changes).
- **Threads:** the main thread (UI, state, param info) vs the audio thread (process). Every API call documents which one it's on.
- **Parameters & automation:** the host owns the timeline. Parameter changes arrive as *events* in process, and
  UI gestures (begin/end) must be reported so automation records cleanly.
- **State:** the host asks for an opaque blob to save in the project, and gives it back on load.
- **Buses and ports:** note input port, audio output port (stereo).
- **Transport:** tempo, playing state, and position (needed for tempo-synced LFOs and delays).
- Formats: **CLAP** (C ABI, MIT, simple, sample-accurate events, polyphonic modulation), **VST3** (C++,
  COM-like, now MIT since 3.8), AU (macOS), AAX (Pro Tools).

**C++ focus.** ABI vs API. Why plugin APIs are C (C++ has no stable ABI across compilers). `extern "C"`,
`__declspec(dllexport)`, function-pointer tables as hand-made vtables, and `void*` back-pointers to your C++ object.

**Build steps**
1. Read `[CLAP]`'s `plugin.h`, `entry.h`, `process.h`, `events.h`, and `ext/params.h`. The header comments are the spec.
2. Draw a sequence diagram: DAW start → scan → insert plugin → play → automate → save → close.
3. **[DECISION D2: final] [DECISION D13]** Which DAW, and which plugin path?

**[THINK]** Why does CLAP pass parameter changes as events inside `process()` rather than via setter calls?

**Exit criteria**
- [ ] A sequence diagram in `docs/` explaining the lifecycle and which thread each call runs on.
- [ ] D2 and D13 decided.

---

## L3.2: CLAP by hand

**Why.** This is the plumbing every framework hides. After this lesson, JUCE and VST3 will feel familiar.

**Concepts**
- `clap_entry` → `get_factory(CLAP_PLUGIN_FACTORY_ID)` → `create_plugin` → a `clap_plugin` struct of callbacks.
- Extensions via `get_extension(id)`: `audio-ports`, `note-ports`, `params`, `state`, later `gui`.
- `process()`: iterate `in_events` (sorted by time). They're already the sample-accurate split you built in L1.7!
  Map `CLAP_EVENT_NOTE_ON/OFF` and `CLAP_EVENT_PARAM_VALUE` to `fss::Event`. Report `NOTE_END` when voices finish (needed for polyphonic modulation).
- Params extension: count, info (from your param table!), get value, value ↔ text, and `flush` (param changes while not processing).
- Thread-safety between `params.get_value` (main thread) and process (audio thread): use the patterns from L2.3/L2.4.

**C++ focus.** Static trampolines (`static void s_destroy(const clap_plugin* p) { self(p)->destroy(); }`),
object lifetime across a C boundary, CMake `SHARED` library with a `.clap` suffix, symbol visibility.

**Architecture / code shape**
```cpp
class FssClap {
public:
    explicit FssClap(const clap_host* host);
    clap_plugin plugin_;            // .plugin_data = this; function pointers → static trampolines
    bool activate(double sr, uint32_t minFrames, uint32_t maxFrames);  // engine_.prepare()
    clap_process_status process(const clap_process*);                   // translate → engine_.process()
private:
    const clap_host* host_;
    fss::Engine engine_;
    std::vector<fss::Event> eventScratch_;   // reserved in activate(), never grows in process
};
```

**Build steps**
1. Get the minimal plugin loading in **FL Studio** (2024.1+ scans CLAP plugins alongside VST), outputting silence.
   Install the `.clap` to `C:\Program Files\Common Files\CLAP` (or add your build folder to FL's plugin search paths) and rescan.
   REAPER's trial is a handy second host with very readable error reporting.
2. Note port + audio port + process translation, so it plays notes.
3. Params extension generated from your param table. Automate cutoff in the DAW.
4. State extension: save and load (simple for now; L3.4 makes it robust).
5. Run `[CLAP-VALIDATOR]` and fix everything it reports.

**[THINK]** What does your plugin do if the host calls `process()` with 0 frames? With a block larger than
`maxFrames`? With events but no audio buffers? (Hosts do all of these.)

**Exit criteria**
- [ ] FSS plays in a CLAP host, automates, and saves/restores with the project.
- [ ] clap-validator passes.

---

## L3.3: Reaching every DAW: VST3 via clap-wrapper or JUCE

> **Deferred (D2):** FL Studio runs CLAP natively, so this lesson moves to Phase G, near the end of the project.

**Why.** If your main DAW is Ableton Live (or anything else without CLAP), you need VST3. It's also how you
share FSS with friends who use those DAWs.

**Options** (D13)
- **`[CLAP-WRAPPER]`:** builds a VST3 (and AUv2/standalone) from your CLAP, using the VST3 SDK (MIT since 3.8).
  Smallest change; keeps your hand-written layer as the source of truth.
- **JUCE 8 wrapper** (`hosts/juce`): an `AudioProcessor` that owns an `fss::Engine`. Gives you VST3/AU/Standalone plus the JUCE
  GUI toolkit. Licensing: AGPLv3, or a JUCE licence (Starter tier is free under a revenue cap). Check current terms.
- **Raw VST3 SDK:** educational about COM-style interfaces, but a lot of boilerplate per unit of insight.

**Concepts.** VST3's split between Processor and Controller (and how wrappers paper over it), parameter normalization
(0..1 everywhere in VST3), MIDI CC handling in VST3 (no raw MIDI; CCs become parameters), plugin IDs/GUIDs.

**Build steps**
1. Build the VST3 through your chosen path. Load it in your DAW.
2. Run `[PLUGINVAL]` at increasing strictness (aim for level 5+ now, 10 by L6.4).
3. Test: two instances at once, sample-rate change mid-session, offline bounce, project reload.

**Exit criteria**
- [ ] FSS works in your main DAW. ★ Use it on a real track, even a sketch.
- [ ] pluginval passes at strictness ≥ 5.

---

## L3.4: State, presets & versioning

**Why.** A synth without presets is a toy, and a preset format without versioning is a trap you spring
on your future self.

**Concepts**
- Plugin state = the patch (all params) + non-param state (e.g. a loaded wavetable) + a version number.
- Forward/backward compatibility: stable param IDs, defaults for missing params, migrations for changed meanings.
- Binary vs text (D15). Endianness. Validation (never trust a loaded blob, since it may be corrupted or hand-edited).
- Preset files on disk, a factory bank (compiled in or shipped as files), and the "init patch".

**C++ focus.** Serialization design, `std::span<std::byte>`, `std::expected` (C++23) or a result type for error handling,
and fuzz-ish tests (random bytes must never crash the loader).

**Build steps**
1. **[DECISION D15]** Format.
2. Implement save/load with a version header. Test round-trips for every parameter.
3. Simulate the future: add a new param, load an "old" preset, and check it gets the default.
4. Preset browse/load in the standalone (and later the GUI).

**Exit criteria**
- [ ] Round-trip and old-version tests pass. Corrupted input is rejected gracefully.
- [ ] Ten saved presets you made.

---

## L3.5: GUI I: editor architecture & threads ★ Milestone M5

**Why.** You need to *see* an FM synth to program it: ratios, algorithm diagrams, envelopes. The GUI is
also a lesson in threads, since the UI must never block the audio and the audio must never wait on the UI.

**Concepts**
- Editor lifecycle: the host gives you a parent window (HWND); you create a child; resizing; DPI scaling.
- Bidirectional binding: knob → `begin gesture` → value changes → `end gesture` (the host records automation); host automation → knob moves.
- Audio → UI data (meters, envelope positions, voice activity) via a lock-free queue or atomics, polled at about 30–60 Hz by a UI timer.
- Layout for FM: an operator panel ×4 (ratio, level, envelope), an algorithm picker *with a diagram*, filter, LFOs, master.

**Options (D14):** JUCE components · Dear ImGui (fast to build; immediate mode; nice for scopes) ·
VSTGUI · a WebView (HTML/CSS/JS UI, popular in 2024–26) · hand-rolled drawing.

**C++ focus.** Observer patterns without locks, timer-driven polling, separating view state from model state, and keeping
the UI layer away from `fss_core` (the core must not know the GUI exists).

**Build steps**
1. **[DECISION D14]**
2. A functional editor: every param has a control (generated from the param table, of course).
3. Gestures correctly reported, automation recorded and played back, and the knobs follow automation.
4. Make it *nice enough to use*: group the operators and draw the algorithm.

**Exit criteria (M5)**
- [ ] You can program a new sound entirely in your DAW using FSS's editor.
- [ ] Automation records from knob gestures and plays back.

---

## L3.6: GUI II: scope, your own FFT, spectrum analyzer

**Why.** Building an analyzer teaches the **FFT**, the most important algorithm in DSP, and gives you a
sound-design tool that shows FM sidebands live.

**Concepts**
- The DFT as correlation with complex sinusoids; the FFT (radix-2 Cooley–Tukey) as divide and conquer; bit-reversal.
- Windowing (Hann, Blackman-Harris) and why (spectral leakage). Resolution vs time trade-offs. Overlap.
- Magnitude in dB, log-frequency display, smoothing and decay ballistics for display.
- An oscilloscope with trigger (zero-crossing) so the waveform stands still.

**C++ focus.** `std::complex<float>`, an in-place iterative FFT, precomputed twiddles, a test against a naive O(N²) DFT,
and a benchmark against a library (pocketfft or KissFFT) for humility.

**Build steps**
1. Read `[JOS-MDFT]` (DFT chapters) and `[JOS-SASP]` (windows). Implement the naive DFT, then a radix-2 FFT, and test them against each other.
2. Audio thread → queue → UI thread → FFT → draw. (Never FFT on the audio thread for display.)
3. Use it: watch sidebands move as you turn the FM index. Watch aliasing appear at high notes.

**Exit criteria**
- [ ] Your FFT matches the naive DFT to ~1e-5 relative error.
- [ ] A live analyzer and scope in the FSS editor (or the standalone).

**Stretch.** Real-input FFT optimization (pack N reals as N/2 complex values). Split-radix.
