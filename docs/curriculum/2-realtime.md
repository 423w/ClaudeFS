# Branch 2: Real-Time

**Branch goal:** a standalone Windows app (`hosts/standalone`) that plays `fss_core` live from a MIDI
keyboard, with knobs, with no glitches, and with a known CPU cost. The DSP doesn't change; you learn
the *environment* DSP runs in: deadlines, threads, and lock-free communication.

**Why a standalone before a plugin?** In a plugin the host hides the device and thread setup. Doing it yourself
once shows you what the host is doing for you. The standalone also becomes your fast dev loop (no DAW restart).

---

## L2.1: Audio callbacks, buffers, latency

**Why.** Real-time audio is a *pull* model. The device asks you for N samples every N/fs seconds, and
if you're late, the user hears a click. Everything in this branch follows from that.

**Concepts**
- The audio callback model: buffer (period) size, double/triple buffering, the deadline (`N/fs`, e.g. 256/48000 ≈ 5.3 ms).
- Latency = buffer size(s) + driver/hardware overhead. Musicians feel latency above about 10 ms.
- Windows audio stack: **WASAPI** (shared vs exclusive mode), **ASIO** (pro low-latency drivers; the SDK went
  GPLv3-compatible in late 2025), WDM-KS. Shared-mode WASAPI resamples to the system rate.
- Sample-format negotiation (float32 vs int16/24), channel counts, and devices with sample rates other than what you assumed.

**C++ focus.** Wrapping a C library cleanly (RAII device handle). Callbacks with `void* userData` →
`static_cast<Engine*>`. Lambdas can't become C function pointers if they capture. Why not?

**Build steps**
1. **[DECISION D9]** Backend. (Recommendation: `[MINIAUDIO]`. Raw WASAPI is a stretch goal.)
2. Open the default device, play a sine from your `Phasor` in the callback, and print the actual sample rate and buffer size.
3. Then call `Engine::process` instead, with a hardcoded note-on at start.
4. **Deliberately break it:** `Sleep(10)` inside the callback, or allocate a big vector there. Listen to the glitches. Remove it.
5. Try buffer sizes 64 / 256 / 1024 and feel the latency differences when you trigger a note from the keyboard (computer keys are fine for now).

**[THINK]**
- The callback runs on a thread you didn't create. What does that imply for any data it shares with `main()`?
- Why might the device give you a different buffer size than you asked for, and a *different one each callback*?

**Exit criteria**
- [ ] The engine plays through the device, and you can compute your output latency.
- [ ] You've *heard* a deadline miss and can explain it.

**Stretch.** Raw WASAPI in event-driven shared mode (COM, `IAudioClient`, `IAudioRenderClient`).

---

## L2.2: The real-time rules

**Why.** Code that is "fast on average" isn't good enough. It has to be fast *every single time*. Several
ordinary C++ operations have unbounded worst-case time.

**Concepts** (`[BENCINA]`, `[RT101]`)
- On the audio thread, never: allocate or free (`new`, `delete`, `malloc`, growing a `std::vector`, `std::string`,
  `std::function` with captures), lock a mutex (**priority inversion**), do file, console, or network I/O, call
  OS APIs that might block, or throw exceptions.
- Also beware: denormals (L2.6), first-touch page faults, `std::shared_ptr` refcount drops freeing memory on the audio thread.
- How to *enforce* it: a debug-only allocation trap (override global `operator new` to assert when a
  thread-local `inAudioThread` flag is set), code review, and RealtimeSanitizer (RTSan, `-fsanitize=realtime`
  in recent Clang; Linux/macOS today. Check whether it supports Windows by the time you get here).

**C++ focus.** Global `operator new`/`delete` replacement. `thread_local`. `noexcept`. RAII "scoped
flag" guards. Reading the standard library's allocation behavior (which calls allocate?).

**Build steps**
1. Read `[BENCINA]` fully. Watch `[RT101]` part I.
2. Add `fss::util::RealtimeGuard` (debug-only) and the allocation trap. Mark `Engine::process` with it.
3. Audit `fss_core`: run the standalone in Debug under the trap, play wildly, and fix anything that trips.
4. Write `docs/RT-RULES.md`, a one-page checklist in your own words.

**[THINK]** Logging is essential for debugging. How do you log from the audio thread? (Hint: L2.3's queue.)

**Exit criteria**
- [ ] The allocation trap is in place, and the engine passes it under heavy playing.
- [ ] Your own RT-rules checklist exists and CLAUDE.md links to it.

---

## L2.3: MIDI input & your first lock-free queue

**Why.** MIDI arrives on a *different thread* from audio. You need to move it to the audio thread without
locks, which means a **single-producer single-consumer (SPSC) ring buffer**. Writing one correctly is
a rite of passage in concurrent C++.

**Concepts**
- MIDI 1.0 messages `[MIDI-SPEC]`: status byte (type + channel) plus 1–2 data bytes. Note On `0x9n`, Note Off `0x8n`
  (and Note On with velocity 0 is a note off!), CC `0xBn`, Pitch Bend `0xEn` (14-bit), Channel Pressure `0xDn`. Running status.
- Windows MIDI: WinMM `midiInOpen` with a callback (simple, and works on Windows 10). (Windows MIDI Services / MIDI 2.0 is Windows 11-oriented.)
- Timestamps: MIDI arrives "now", but the audio block covers a time range. Mapping MIDI time to a sample offset is a classic jitter problem.
- **SPSC ring buffer:** fixed capacity (a power of 2, so you can mask instead of mod), a write index owned by the producer,
  a read index owned by the consumer, and `std::atomic` indices with **acquire/release** ordering. Why it's lock-free *and* wait-free.
- False sharing: put the indices on separate cache lines (`alignas(64)`, or `std::hardware_destructive_interference_size`).

**C++ focus.** `std::atomic`, `memory_order_acquire/release/relaxed` (read `[PRESHING]`), templates
(`SpscQueue<T, Capacity>`), `static_assert` (capacity is a power of two, `T` is trivially copyable), and stress
testing concurrent code (two threads, millions of items, check ordering and that nothing is lost).

**Architecture / code shape**
```cpp
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0);
    std::array<T, Capacity> buf_;
    alignas(64) std::atomic<std::size_t> head_{0};   // written by producer
    alignas(64) std::atomic<std::size_t> tail_{0};   // written by consumer
public:
    bool push(const T&);   // producer only
    bool pop(T&);          // consumer only
};
// MIDI callback → parse → push(Event) ; audio callback → drain → Engine::process(events)
```

**Build steps**
1. **[DECISION D10]** WinMM by hand vs RtMidi/libremidi.
2. Write `SpscQueue` *yourself*, plus a two-thread stress test. Then compare it to `[MOODYCAMEL]`. What did they do differently?
3. MIDI parser to `fss::Event`, with unit tests from raw byte sequences (including running status and velocity-0 note-offs).
4. Wire MIDI to the queue to the engine. No keyboard? Use `loopMIDI` + `VMPK`.
5. Bonus: an audio-thread → main-thread queue for log messages and voice-count display.

**[THINK]**
- What happens if the queue is full? Who should drop data, and what should get dropped?
- Why does `relaxed` ordering suffice for one of the two loads in `push()`?

**Exit criteria**
- [ ] You play FSS from a keyboard. 🎹
- [ ] The SPSC stress test passes millions of items across threads, in Release and Debug.
- [ ] You can explain acquire/release in your own words with a diagram.

---

## L2.4: Parameters, smoothing & a dev control surface ★ Milestone M3

**Why.** A synth is played with knobs as much as keys. Parameter changes cross threads, need smoothing
to avoid zipper noise, and must be described *once* so host, UI, and presets all agree.

**Concepts**
- The parameter table (Architecture principle 5): stable IDs, ranges, defaults, **skew** (cutoff is exponential,
  times are log-ish, levels are dB), display formatting, and normalized (0..1) vs plain values.
- Zipper noise: stepping a gain or cutoff once per block is audible. **Smoothing:** a one-pole (from L1.9!) or a linear
  ramp over N ms. Per-parameter smoothing times. What *not* to smooth (discrete choices like algorithm or waveform).
- Cross-thread transport: atomics per parameter vs events in the queue (sample-accurate). Change detection.
- MIDI CC mapping: CC 74 → cutoff, mod wheel CC 1 → vibrato, and so on. Even without a GUI, a cheap MIDI controller makes FSS playable.

**C++ focus.** `constexpr std::array<ParamInfo, N>` as the single source of truth. `std::string_view`.
Maybe X-macros or a small code generator. A templated `Smoothed<T>` value. Possibly `std::bit_cast` for atomic float tricks.

**Build steps**
1. **[DECISION D11, D12]** Transport model and dev UI (MIDI CCs only, Dear ImGui, or both).
2. Build the param table for everything in the `Patch` so far. Write a test that every param has a unique ID and a default inside its range.
3. Implement smoothing. Do an A/B render: unsmoothed vs smoothed cutoff jumps (spectrogram both).
4. Map CCs to params, and/or build an ImGui window with a slider per param (generated from the table!).

**[THINK]**
- If the param table drives everything, what happens to old presets when you reorder or add params? (Stable IDs, not indices.)
- Should smoothing live in the parameter system or in each DSP primitive?

**Exit criteria (M3)**
- [ ] You play FSS live and tweak every parameter with no zipper noise.
- [ ] The param table is the only place parameters are defined.

---

## L2.5: Playing it like an instrument

**Why.** The engine works. Now make it *feel* good. This is where musicianship meets engineering.

**Concepts**
- Voice stealing done right: steal policy, fast fade (D8 revisited), "same note retrigger" behaviors.
- **Mono mode** and **legato** (don't retrigger envelopes when notes overlap), **portamento/glide**
  (constant time vs constant rate; always-on vs legato-only), and note priority (last, low, high) with a held-note stack.
- **Sustain pedal** (CC 64: notes held until the pedal is released), pitch bend (smoothed, range), mod wheel, aftertouch.
- (Optional) MPE: per-note pitch, pressure, and timbre. CLAP supports per-note expressions natively.

**C++ focus.** A small fixed-capacity stack or array for held notes (no allocation!). State-machine edge cases. Testing input
*sequences* (given these events, these voices should be in these states).

**Build steps**
1. Implement mono/legato/glide and the held-note stack.
2. Implement sustain pedal semantics (a note released while the pedal is down stays in a "sustained" state).
3. Scenario tests: write event sequences and expected voice states (pedal down, note on, note off, pedal up, and so on).
4. Play a mono FM bass with legato glide. Play sustained chords.

**[THINK]** Which behaviors should be *parameters* (user choice) and which should be fixed design choices?

**Exit criteria**
- [ ] Scenario tests for legato, pedal, and stealing.
- [ ] A mono legato bass line and pedal-sustained chords played live, with no stuck notes.

---

## L2.6: CPU budget, profiling & denormals ★ Milestone M4

**Why.** You have about 5 ms per block, shared with everything else in the DAW. Know your cost,
and know the classic traps that silently eat CPU.

**Concepts**
- Measuring: time each `process()` call (`std::chrono::steady_clock` / `QueryPerformanceCounter`) and
  compare it to the block duration, which gives a "DSP load %". Worst case matters more than average.
- Profilers: Visual Studio's CPU profiler, Superluminal, Tracy (frame-style instrumentation, nice for audio).
- **Denormals:** tiny floats (< 1e-38) as filter and reverb tails decay to silence are 10–100× slower on many CPUs.
  Fix: set FTZ/DAZ (`_mm_setcsr` flags) at the start of `process()`. That's the `DenormalGuard` RAII object.
- Cheap wins: compute per-block instead of per-sample where it's inaudible; skip idle voices; hoist branches.
- Debug vs Release: never judge DSP performance in Debug.

**C++ focus.** RAII for processor state (save and restore MXCSR). `<immintrin.h>` (your first intrinsic).
Microbenchmarks (pitfalls: the optimizer deleting your benchmark).

**Build steps**
1. Add a DSP-load meter to the standalone.
2. Reproduce a denormal spike: a resonant filter fed an impulse and then silence. Measure the CPU over time with and without FTZ/DAZ.
3. Profile 16 voices playing. Find the top 3 hot spots, fix one, and measure again.

**Exit criteria (M4)**
- [ ] You know your per-voice cost in µs/block.
- [ ] The denormal guard is in place and the spike is demonstrated gone.
- [ ] FM → resonant SVF → amp, with envelopes and LFOs, played live with 16 voices under budget.
