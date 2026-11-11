# Branch 1: The Offline Engine

**Branch goal:** build `fss_core`, a dependency-free C++ DSP library that turns a *patch* and a *list
of notes* into a WAV file. No real-time pressure, no threads, no devices. Just numbers becoming sound,
rendered, listened to, plotted, and tested.

**Why offline first?** You can render, inspect any sample, plot it, diff it against a known-good
file, and run it in a debugger without glitching. Every primitive you write here gets reused unchanged
in the real-time host and the plugin.

**Shape of the work:** each lesson adds a primitive to `core/include/fss/dsp/` or `synth/`, a test, and a
render scenario to `tools/render`, then you *listen* and *plot*.

> Route note: L1.1–L1.7 come before Branch 2. L1.8–L1.11 are designed to be done *after* you can play
> live (L2.1–L2.4), so you can hear filters and LFOs in real time. See the route in [ROADMAP.md](../ROADMAP.md).

---

## L1.1: Digital audio & writing a WAV file from scratch

**Why.** Before any synthesis, you need to get samples out of your program and into your ears. Doing
it from scratch teaches what a sample *is* and what a binary file format looks like.

**Concepts**
- Samples, sample rate, bit depth; integer PCM vs 32-bit float; full scale (±1.0f); clipping.
- Interleaved vs planar channel layout.
- The RIFF/WAVE format: `RIFF` chunk → `WAVE` → `fmt ` chunk (format tag 1 = PCM, 3 = IEEE float) →
  `data` chunk. Little-endian. Sizes you have to patch at the end.
- Dither (just know it exists for 16-bit export).

**C++ focus.** Binary I/O with `std::ofstream` (`std::ios::binary`), writing little-endian integers
portably (and why `reinterpret_cast`-ing structs is a trap: padding, endianness), `std::span<const float>`,
RAII (file closed and sizes patched in the destructor, or an explicit `finish()`. Which is better, and why?).

**Architecture / code shape**
```cpp
// tools/render/wav_writer.h   (lives outside core: core never does I/O)
class WavWriter {
public:
    WavWriter(std::filesystem::path, int sampleRate, int numChannels, SampleFormat fmt);
    void writeInterleaved(std::span<const float> frames);
    ~WavWriter();          // patches RIFF and data sizes
};
```
First render: `x[n] = 0.5f * std::sin(2π · 440 · n / fs)` for 2 seconds.

**Build steps**
1. Read a WAV format description (search "WAVE PCM soundfile format", the classic Stanford CCRMA page).
2. Implement `WavWriter`, supporting 32-bit float and 16-bit PCM (with clamping).
3. Render a 440 Hz sine, open it in Audacity, and check the spectrum with your `plot_spectrum.py`.
4. Render the same sine at amplitude 1.5 in 16-bit and listen to the clipping. Plot its spectrum. Where did the new harmonics come from?

**[THINK]**
- Why do audio programs prefer float internally?
- What happens at the *start* and *end* of your sine (a click?). Why? Keep this question in mind until L1.3.

**Exit criteria**
- [ ] WAVs you write open correctly in Audacity, in your DAW, and in Python.
- [ ] A unit test reads back a written file and checks the header fields.
- [ ] You can explain the click at the start and end of the file.

**Stretch.** A `WavReader` (you'll want it for wavetables in L4.3 and golden-file tests in L6.1).

---

## L1.2: The phase accumulator & sine oscillator

**Why.** Every oscillator in FSS (operators, LFOs, wavetables) is built on a **phasor**: a number
that ramps from 0 to 1 and wraps, advancing by `f/fs` each sample. Phase is the central concept of FM.

**Concepts**
- Why `sin(2π f n / fs)` with a growing `n` is fragile (float precision loss on long notes; frequency
  changes cause phase jumps, which are clicks).
- Phase accumulator: `phase += inc; if (phase >= 1) phase -= 1;` where `inc = f / fs`.
- Changing frequency mid-note: the phase stays continuous, so no clicks, and **glide** comes for free.
- Ways to compute sine: `std::sin`, lookup table + linear interpolation, polynomial approximations.
  Accuracy (measure the error in dB) vs speed.
- How the DX7 did it: a 32-bit phase, a quarter-wave **log-sine** table, and an exponential table (`[SHIRRIFF-DX7]`).

**C++ focus.** Small value-type classes. `constexpr`-built lookup tables (a `std::array` filled by a
`constexpr` function, or generated at `prepare()`). `[[nodiscard]]`. Unsigned integer wraparound
as *defined* behavior (vs signed overflow, which is UB).

**Architecture / code shape**
```cpp
namespace fss::dsp {
class Phasor {
public:
    void setFrequency(double hz, double sampleRate) noexcept { inc_ = hz / sampleRate; }
    void reset(double phase = 0.0) noexcept { phase_ = phase; }
    [[nodiscard]] double tick() noexcept {          // returns phase in [0,1)
        const double p = phase_;
        phase_ += inc_;
        phase_ -= std::floor(phase_);               // handles negative inc too
        return p;
    }
private:
    double phase_ = 0.0, inc_ = 0.0;                // [DECISION D5]
};
float sine01(double phase);   // sin(2π·phase): try std::sin, table, polynomial
}
```

**Build steps**
1. **[DECISION D5]** Phase type. (Suggestion: `double` now, and try `uint32_t` as a stretch.)
2. Implement `Phasor` and three `sine01` versions. Write a test that compares them to `std::sin` and
   reports max error in dB. Benchmark them (Catch2 `BENCHMARK` or a simple timer, in Release builds!).
3. Render a 2-second glide from 110 Hz to 880 Hz (exponential sweep: interpolate *pitch*, not Hz). No clicks allowed.
4. Render a chord of three sines. Notice they're just summed. (Watch the level: scale by 1/3.)

**[THINK]**
- An exponential glide sounds "even" and a linear-Hz glide doesn't. Why? (Back to L0.2.)
- What's the precision of a `float` near 1.0? How many samples until a float phase accumulator at 0.1 Hz goes wrong?
- Why would a 1990s chip use a log-sine table plus an exponential table instead of multiplying?

**Exit criteria**
- [ ] Glide render is click-free (zoom in on the waveform) and the spectrogram shows a smooth curve.
- [ ] A table-based sine with error < −90 dB, or an explanation of why your version stops where it does.
- [ ] You can explain phase vs frequency without notes.

**Stretch.** A `uint32_t` phasor (`inc = round(f/fs · 2^32)`), with the table indexed by the top bits.

---

## L1.3: Envelopes: ADSR as a state machine

**Why.** Sound has *shape over time*. In FM, every operator has its own envelope, and an envelope on a
*modulator* changes **timbre**, not just loudness (the brass "blat"). This is why envelopes are
central to FSS, and it also fixes the clicks from L1.1.

**Concepts**
- Attack, Decay, Sustain (a *level*, not a time), Release. Gate on and off.
- Linear vs exponential segments. Why exponential sounds natural (perception is logarithmic, and analog RC circuits behave that way).
- The one-pole "approach a target" trick: `level += coeff * (target - level)`. Overshooting the target
  slightly so segments actually finish (`[EARLEVEL]` "Envelope generators—ADSR code").
- Edge cases: release from *any* stage (from the current level, not the sustain level), retrigger while releasing
  (from zero, or from the current level? Legato?), zero-length segments, sample-rate independence (times in ms).
- Clicks are discontinuities. Any instant gain jump of a non-zero signal clicks.

**C++ focus.** `enum class Stage { Idle, Attack, Decay, Sustain, Release }`, `switch`-based state
machines, keeping per-sample code branch-light, `std::exp`, and computing coefficients in `prepare()`/setters instead of per sample.

**Architecture / code shape**
```cpp
namespace fss::dsp {
struct AdsrParams { float attackMs, decayMs, sustain, releaseMs; };
class Adsr {
public:
    void prepare(float sampleRate);
    void setParams(const AdsrParams&);      // recompute coefficients here
    void noteOn();  void noteOff();
    [[nodiscard]] float tick();             // 0..1
    [[nodiscard]] bool isActive() const;    // Idle → the voice can be freed (L1.7)
private:
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Sustain, Release } stage_{};
    float level_ = 0.f; /* coefficients... */
};
}
```

**Build steps**
1. **[DECISION D6]** ADSR vs DX-style rate/level (start with ADSR).
2. Implement it. Plot the envelope itself (render it as audio, a DC signal from 0 to 1, and plot it).
3. Apply it to the sine. Render a pluck (A=1 ms, D=300 ms, S=0), a pad (A=800 ms, S=0.8, R=1.5 s), and an organ (A=5 ms, S=1, R=10 ms).
4. Test it: note-off during attack releases smoothly from the current level, and the output stays finite and inside [0,1].

**[THINK]**
- With A=0 ms, do you hear a click? Should "0 ms" really mean zero? (Many synths enforce a minimum. What would you choose?)
- What should "sustain = 0.5" mean, 0.5 linear or −6 dB? Who's the user of that knob?

**Exit criteria**
- [ ] No clicks at note-on or note-off in any of the three renders (zoom in to check).
- [ ] Tests for release-from-attack, retrigger, and zero-time segments.
- [ ] Envelope times are correct at both 44.1 kHz and 96 kHz (measure the time to −60 dB, for example).

---

## L1.4: Two-operator FM/PM: sidebands & Bessel functions ★ Milestone M1

**Why.** This is the heart of the synth. One sine (the *modulator*) wiggles the phase of another (the
*carrier*), and a whole spectrum appears. Learning to *predict* that spectrum is the most valuable
DSP intuition in this project.

**Concepts**
- FM: `y(t) = sin(2π·fc·t + I·sin(2π·fm·t))`. **Index** `I` = peak phase deviation in radians (`Δf/fm`).
- Sidebands appear at `fc ± k·fm` (k = 1, 2, 3...) with amplitudes `J_k(I)`, the **Bessel functions** of the first kind.
  You don't need to derive them. Plot `J_0..J_5` vs `I` (scipy has `jv`) and see why timbre shifts non-monotonically as `I` grows.
- Bandwidth rule of thumb (Carson's rule): about `2·fm·(I + 1)`.
- **C:M ratio:** integer ratios give harmonic spectra (a pitched tone); irrational ratios give inharmonic spectra (bells, metal).
  Negative-frequency sidebands **reflect** around 0 Hz (with a phase flip) and interfere with positive ones.
- **FM vs PM:** modulating the *frequency* integrates the modulator into phase; modulating the *phase* adds directly.
  For a sine modulator they're nearly equivalent, but PM keeps pitch stable when the modulator has DC or is itself modulated.
  The DX7 "FM" is actually **PM**. So is FSS.
- Index envelopes: brass = index rising with amplitude (Chowning's example); bell = slow-decaying inharmonic tone.

**C++ focus.** Composition: `Operator` = `Phasor` + `Adsr` + level. A function signature that makes
the modulation input explicit: `float tick(float phaseModRadians)`.

**Architecture / code shape**
```cpp
namespace fss::synth {
class Operator {
public:
    void prepare(float sampleRate);
    void setFrequency(double hz);
    void noteOn(); void noteOff();
    // pm is in "phase units" (cycles), added to the phasor's phase before the sine lookup
    [[nodiscard]] float tick(float pm) {
        const double ph = phasor_.tick() + pm;
        return dsp::sine01(ph - std::floor(ph)) * env_.tick() * level_;
    }
private:
    dsp::Phasor phasor_; dsp::Adsr env_; float level_ = 1.f;
};
}
// 2-op: out = carrier.tick( index * modulator.tick(0) );
```
(Watch the units. Decide whether `index` is in radians or cycles and document it. This is a classic bug source.)

**Build steps**
1. Read `[CHOWNING73]` (all of it; it's short) and `[PUCKETTE]`'s chapter on modulation.
2. Plot Bessel functions in Python. **Predict** the spectrum for fc=440, fm=440, I=1, then for I=3.
3. Implement `Operator` and render 2-op patches. Use `plot_spectrum.py` to check your predictions:
   sideband positions exactly, and amplitudes roughly.
4. Recreate three of Chowning's sounds: a **bell** (fc:fm = 1:1.4, long decay, index decaying with amplitude),
   **brass** (1:1, index following the amp envelope, fast attack), and a **wood drum** or **clarinet** (odd ratio 1:2 / 3:2).
5. Write it up in your journal, with spectra.

**[THINK]**
- Why does increasing the index sometimes make a partial *quieter*? (Look at the Bessel plots.)
- At fc=1000, fm=1000, I=10, where's the highest sideband? At fs=48k, are you aliasing? (FM aliases too, and it comes back in L4.2.)
- Why is a modulator *envelope* the main expressive control in FM?

**Experiments.** Sweep the index slowly from 0 to 10 over 10 seconds and watch the spectrogram. Sweep the
ratio from 1.0 to 2.0 and listen to the "beating" as it passes non-integer values.

**Exit criteria (M1)**
- [ ] Measured sideband frequencies match `fc ± k·fm` for at least two test cases.
- [ ] A bell and a brass tone you'd actually use, rendered to WAV.
- [ ] You can explain FM vs PM, and why we chose PM.

---

## L1.5: Operators, algorithms & feedback (the FM voice)

**Why.** Real FM synths use 4–8 operators in configurable routings (**algorithms**). Designing that
routing is a lovely architecture problem: a graph, its evaluation order, and data vs code.

**Concepts**
- Stacks (A→B→C), parallel modulators (A+B→C), multiple carriers (organ-like additive mixing).
- **Feedback:** an operator modulating itself through a one-sample delay. Feedback gives a saw-ish
  wave and, at extremes, noise. The DX7 averages the last two outputs to tame instability.
- Evaluation order: modulators must be computed before what they modulate in the same sample. That's a
  topological order. A free matrix with cycles needs one-sample delays for the back edges.
- Output normalization: with N carriers, scale so loudness stays consistent.

**C++ focus.** Data-driven design: `constexpr` algorithm tables. `std::array<std::array<float,N>,N>`.
Templates on operator count (`template <int N>`) vs a runtime-sized loop. Unrolling. Separating *description*
(the `Algorithm` struct) from *execution* (the `FmVoice::tick` loop).

**Architecture / code shape**
```cpp
constexpr int kNumOps = 4;                        // [DECISION D7]
struct Algorithm {
    std::array<std::array<float,kNumOps>,kNumOps> mod{};   // mod[src][dst]
    std::array<float,kNumOps> carrier{};                   // output mix
    // evaluation order precomputed; back-edges read last sample's output
};
inline constexpr std::array<Algorithm, 8> kAlgorithms = { /* stack, 2+2, 3→1, ... */ };

class FmVoice {
    std::array<Operator,kNumOps> ops_;
    std::array<float,kNumOps> prevOut_{};         // for feedback / back edges
public:
    float tick(const Algorithm& alg, float feedback);
};
```

**Build steps**
1. Read `[SHIRRIFF-DX7]` part 4 (how algorithms are implemented in hardware) and skim msfa's `fm_core` `[MSFA]`.
2. **[DECISION D7]** Operator count and routing model. (Recommendation: 4 ops, free matrix, preset algorithms.)
3. Implement `FmVoice` with feedback on at least one operator. Define 8 algorithms as data.
4. Render: an **E-piano** (two 2-op stacks in parallel: a tine stack at 1:14-ish with a fast-decaying modulator, plus a body stack at 1:1),
   an **FM bass** (stack with feedback), and an **organ** (all carriers at harmonic ratios).
5. Test it: feedback = 0 reproduces the non-feedback result exactly, and output stays bounded for any matrix entry in range.

**[THINK]**
- Fixed algorithms vs a free matrix: what do you gain and lose as a *player* and as a *programmer*?
- What happens to CPU when every op can modulate every op? Does it matter at 4 operators?
- Is feedback order-dependent? What does "one sample delay" mean for a 20 kHz tone?

**[DECISION D1: confirm.]** You've now heard real FM sounds you made. Keep FM-core hybrid, or pivot
(swap operators for VA oscillators; the rest of the curriculum still applies)? Record it in DECISIONS.md.

**Exit criteria**
- [ ] Eight algorithms defined as data, with an ASCII diagram for each in a comment or doc.
- [ ] E-piano, bass, and organ renders.
- [ ] D1 confirmed in DECISIONS.md.

---

## L1.6: The note: pitch, velocity, key scaling

**Why.** An instrument responds to *how* you play. FM is especially expressive: velocity on a modulator
means harder playing gives a brighter tone. This lesson turns "a voice at a frequency" into "a voice playing a note".

**Concepts**
- Operator frequency = note frequency × ratio (× detune in cents), or **fixed frequency** mode (for percussive and inharmonic sounds).
- Coarse/fine ratio design: DX-style (0.5, 1, 2, 3...) plus fine, vs continuous.
- Velocity sensitivity per operator (curves: linear, exponential).
- **Key scaling:** level scaling (modulators quieter on high notes, so they're less harsh and alias less) and
  rate scaling (high notes decay faster, like real instruments).
- Pitch bend range, global tuning (A4 = 440 by default).

**C++ focus.** **Strong types** for units: `Hz`, `Semitones`, `Cents`, `Ratio`, maybe user-defined
literals (`440.0_Hz`). They prevent the "radians vs cycles" and "Hz vs semitones" bugs at compile time. Explicit constructors.

**Architecture / code shape**
```cpp
struct NoteContext { std::uint8_t key; float velocity; double baseHz; float bendSemis; };
struct OperatorParams {
    float ratioCoarse, ratioFine, detuneCents, fixedHz; bool fixedMode;
    float level, velSens, keyLevelScale, keyRateScale;
    dsp::AdsrParams env;
};
// FmVoice::start(const NoteContext&, const PatchParams&) computes per-op frequencies, levels, rates
```

**Build steps**
1. Add a `Patch` struct (all parameters for one sound) and a tiny text score format for `tools/render`:
   `t_ms key vel dur_ms` per line. You've just built a mini sequencer.
2. Implement ratio and fixed modes, velocity sensitivity, and key scaling.
3. Render a melody across 4 octaves with key scaling off, then on. Compare the spectrograms.

**[THINK]**
- Should velocity affect amplitude, timbre, or both? Where in the code does that choice live?
- What would make a strong-type system *annoying*? Find the balance.

**Exit criteria**
- [ ] Melody render from a score file.
- [ ] Key scaling audibly and visibly changes high vs low notes.
- [ ] Unit types catch at least one mistake at compile time. (Deliberately write the bug and watch it fail.)

---

## L1.7: Voices, polyphony & the Engine ★ Milestone M2

**Why.** Chords! This is also where `fss_core` gets its real public API (`Engine`), the one every host
will call. Getting this boundary right pays off for the rest of the project.

**Concepts**
- A **voice** is one sounding note: its own operators, envelopes, filter state, and so on. Polyphony means N voices.
- **Voice allocation:** find a free voice (one whose envelopes are Idle). If none, **steal** one: the oldest,
  the quietest, or the same key. Stealing must not click; give the stolen voice a quick fade (around 5 ms), or use a "release-then-start" scheme.
- Voice lifetime: note-on, note-off (release), then Idle, then free.
- Mixing voices: headroom (N voices can sum to N× amplitude), a master gain, and a safety soft clip.
- Sample-accurate events: split each block at event times (Architecture principle 6).

**C++ focus.** Preallocated object pools (`std::array<Voice, kMaxVoices>`) instead of dynamic allocation.
`std::span<const Event>`. Designing a small, stable public API. `const`-correctness. The block-splitting loop.

**Architecture / code shape**
```cpp
class Engine {
public:
    void prepare(double sr, int maxBlock);
    void process(std::span<const Event> events, float* L, float* R, int n);
private:
    void handleEvent(const Event&);
    void renderVoices(float* L, float* R, int start, int end);
    std::array<Voice, kMaxVoices> voices_;
    VoiceAllocator alloc_;               // [DECISION D8]
    Patch patch_;
};
// process(): idx=0; for each event e: render [idx, e.frame); handle e; idx = e.frame; ... render tail
```

**Build steps**
1. **[DECISION D8]** Voice count and steal policy.
2. Implement `Voice`, `VoiceAllocator`, and `Engine`. `tools/render` now builds `Event` lists from the score and calls `Engine::process` in blocks of, say, 256.
3. **The block-size invariance test:** rendering with block size 1, 64, and 4096 must give *identical* output.
   This test will catch more bugs than any other in the project. Write it now.
4. Render a chord progression and a melody with overlapping notes. Then render a passage that forces voice stealing, and listen for clicks.

**[THINK]**
- Why is "same key retrigger" a separate case? (Think of a piano repeating a note.)
- If voices are freed when their envelopes go idle, which envelope? The amp envelope? All of them?

**Exit criteria (M2)**
- [ ] A short piece (chords + melody) rendered entirely by your engine that you're proud of. Share it with a friend.
- [ ] The block-size invariance test passes.
- [ ] Voice stealing is click-free in a stress render.

> **Now go to Branch 2 (L2.1–L2.4)** and play this live. Come back for L1.8+.

---

## L1.8: Harmonics, classic waveforms & aliasing (PolyBLEP)

**Why.** Saw and square waves are the raw material of subtractive synthesis, and the best way to *truly*
understand aliasing. They become extra operator waveforms in the hybrid (L4.2).

**Concepts**
- Fourier series: saw = all harmonics at 1/n; square = odd harmonics at 1/n; triangle = odd harmonics at 1/n².
- **Naive** waveforms (`2·phase − 1`) have infinitely sharp edges, so infinite harmonics, so aliasing.
  Aliasing is *inharmonic* and moves the opposite way to the pitch. It's ugly once you know to hear it.
- Band-limited generation: additive (exact, expensive), BLIT `[STILSON-BLIT]`, minBLEP, **PolyBLEP**
  `[VALIMAKI07]` (cheap: correct the two samples around each discontinuity with a small polynomial).
- Square via PolyBLEP at both edges. Triangle by integrating the square (leaky integrator) or via BLAMP.

**C++ focus.** Free functions vs classes. `inline` in headers. Function templates over waveform type
(`template <Waveform W> float tick()`) vs a runtime `switch`. Measure both.

**Architecture / code shape**
```cpp
// PolyBLEP residual: t = phase, dt = phase increment
inline float polyBlep(double t, double dt) {
    if (t < dt)       { t /= dt;           return float(t + t - t*t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0)/dt;  return float(t*t + t + t + 1.0); }
    return 0.f;
}
// saw = (2*phase - 1) - polyBlep(phase, inc)
```
(Derive *why* this works from `[VALIMAKI07]` or `[BLEP-SNDKIT]` before you trust it.)

**Build steps**
1. Render a naive saw sweeping from 100 Hz to 8 kHz. Spectrogram it. Find the aliases (the lines moving downward).
2. Render an additive saw (sum of harmonics up to Nyquist) as ground truth.
3. Implement the PolyBLEP saw and square, render the same sweep, and compare all three spectrograms side by side.
4. Measure alias energy (e.g. energy at non-harmonic frequencies) for each method at a few pitches.

**[THINK]**
- PolyBLEP doesn't remove *all* aliasing. Where does the residue live, and is it audible?
- Can you apply PolyBLEP to an FM operator's output? Why not? (Hint: where are the discontinuities?)

**Exit criteria**
- [ ] Three-way spectrogram comparison saved in your journal.
- [ ] You can *hear* aliasing in the naive sweep and point to it.

---

## L1.9: Filters I: one-pole, biquads, frequency response

**Why.** Filters are the core of DSP math and the "subtractive" half of FSS. Start with the simplest
filter (which you'll use *everywhere*, for parameter smoothing too) and build up to biquads.

**Concepts**
- What a filter does: shapes the spectrum. **Impulse response** and **frequency response** (magnitude and phase).
- Difference equations; FIR vs IIR (feedback); stability.
- The one-pole low-pass: `y += a·(x − y)`, with `a = 1 − exp(−2π·fc/fs)`. It's a smoother, a DC-blocker (as a high-pass), and the same thing as an envelope segment!
- Transfer function `H(z)`, poles and zeros (just-in-time z-transform: `z^-1` is a one-sample delay).
  Evaluate `H(e^{jω})` with `std::complex` to *compute* the frequency response.
- The **biquad**: `y = b0x + b1x1 + b2x2 − a1y1 − a2y2`. Coefficients from `[RBJ]`. Direct Form I vs
  Transposed Direct Form II (state behavior and numerical differences).
- Resonance / Q, and cutoff warping near Nyquist (bilinear transform preview).

**C++ focus.** `std::complex<double>`. Separating coefficient computation (slow, on parameter change)
from processing (fast). A test that compares *measured* response (feed a sine sweep or an impulse, then FFT) against *computed* `H(e^{jω})`.

**Build steps**
1. Read `[JOS-FILTERS]` (the "simplest lowpass" chapters, then the biquad sections) and `[DSPGUIDE]` filter chapters. Skim `[EARLEVEL]` biquad posts.
2. Implement `OnePole` (LP/HP) and `Biquad` (LP, HP, BP, notch, peak, shelves).
3. Write `plot_response.py`, or better, a C++ `responseAt(freq)` function and a test that compares it to a measurement.
4. Run the FM bass through a biquad low-pass and render a cutoff sweep.

**[THINK]**
- Sweep a biquad's cutoff fast. Hear the zipper or the "pops"? Why does a biquad dislike fast modulation? (Sets up L1.10.)
- Why is the one-pole coefficient `1 − exp(...)` and not just `fc/fs`?

**Exit criteria**
- [ ] Measured vs computed magnitude response agree within 0.1 dB across the band.
- [ ] One-pole and biquad filters with tests. You can explain poles and zeros at a "what do they do to the spectrum" level.

---

## L1.10: Filters II: virtual analog (TPT/ZDF SVF, ladder)

**Why.** Synth filters get modulated hard (envelopes, LFOs, even audio-rate), and they need to *resonate*
musically and self-oscillate. Topology-preserving transform (TPT) filters, also called "zero-delay feedback" (ZDF), handle this
gracefully. This is the filter you'll actually ship.

**Concepts**
- Analog prototypes: integrators, the RC low-pass, and the state-variable filter (simultaneous LP/BP/HP).
- Discretization: naive (Euler) vs **trapezoidal/bilinear**; "delay-free loops" and solving them (the "zero-delay feedback" part).
- Cutoff prewarping: `g = tan(π·fc/fs)`.
- The **TPT SVF** `[ZAV]`, `[SIMPER-SVF]`: stable under fast modulation, with multiple outputs (LP/BP/HP/notch/peak) from one structure.
- Resonance (`k = 1/Q`), self-oscillation, and gain compensation.
- The **Moog ladder** (4 one-poles + feedback): the classic 24 dB/oct sound. Its nonlinear version (`tanh` in the loop) is a preview of L4.5.
- Filter envelope amount, key tracking (cutoff follows the note), velocity → cutoff.

**C++ focus.** Precision choices (double state for low cutoffs?). Keeping per-sample math tight. A
`FilterMode` enum vs templated outputs. Testing stability: random modulation fuzz tests (output must stay finite).

**Architecture / code shape**
```cpp
class Svf {   // TPT state-variable filter
public:
    void prepare(float sr);
    void set(float cutoffHz, float resonance);   // computes g, k, a1..a3
    struct Out { float lp, bp, hp; };
    Out tick(float x);                           // 2 state variables (ic1eq, ic2eq)
};
// Voice chain: fm.tick() → svf.tick() selected output → amp env
```

**Build steps**
1. Read `[ZAV]`'s chapters on 1-pole filters, time discretization, and the SVF. *Derive* the TPT one-pole
   on paper before coding. Then read `[SIMPER-SVF]` and compare it to your derivation.
2. Implement the `Svf`. Test that its response matches an RBJ biquad at a static cutoff (it should be almost identical!).
3. The modulation torture test: cutoff driven by a 50 Hz sine from 100 Hz to 15 kHz. Compare the biquad and the SVF: listen, plot, and check stability.
4. Add the per-voice filter with a filter envelope and key tracking to `Voice`. Render an FM pluck through a resonant LP with an envelope sweep.
5. (Optional) Implement a linear ladder, then a `tanh` ladder.

**[THINK]**
- Where does the "Q" knob of an analog synth map in your equations? What range feels musical?
- Where in the signal path should the filter go for FM? (Post-carrier mix, of course. What would a pre-carrier, per-operator filter do?)

**Exit criteria**
- [ ] The SVF survives the modulation torture test with no blowups and no zipper.
- [ ] A resonant FM-through-filter patch you love.
- [ ] You can explain, in a paragraph, why the TPT SVF beats the biquad for synth use.

---

## L1.11: LFOs & basic modulation

**Why.** "Do I need an LFO?" Yes. Vibrato, tremolo, filter wobble, and evolving pads all need one. An LFO is just a slow oscillator, so
you'll reuse your phasor. This lesson also introduces the *idea* of routing modulation (generalized in L4.1).

**Concepts**
- Shapes: sine, triangle, saw up/down, square, sample-and-hold (random), smoothed random.
- Rate in Hz or tempo-synced (later, when the host gives you a tempo), delay plus fade-in, key-sync vs free-running, mono (global) vs poly (per voice).
- Unipolar vs bipolar; depth; *where* modulation applies: pitch in semitones (exponential!), amplitude in dB or linear, cutoff in octaves.
- Control rate vs audio rate: computing modulation every N samples saves CPU but causes stair-steps. Trade-offs.
- Modulation of parameters means combining: `final = base + Σ amount·source`, in the right units, and then clamped.

**C++ focus.** Reuse through composition (an `Lfo` wraps a `Phasor`). A small `ModSource` concept.
Random numbers on the audio thread (`std::mt19937` is fine; `rand()` is not; or write a xorshift. Why?).

**Build steps**
1. Implement `Lfo` with all shapes, including S&H, and with delay and fade-in.
2. Hardwire routes: LFO1 → pitch (vibrato), LFO2 → filter cutoff, mod wheel → vibrato depth. Velocity → FM index already exists from L1.6.
3. Render vibrato with delay (a violin-ish feel), a filter wobble bass, and a slow LFO on an operator level (an evolving pad).

**[THINK]**
- Why should vibrato depth be in *semitones* and filter modulation in *octaves*?
- Should every voice's LFO start at phase 0 (key-sync) or share one LFO? What does each sound like on a chord?

**Exit criteria**
- [ ] LFO shapes are verified by plotting.
- [ ] Three modulated renders, and the patch is now "alive".
- [ ] A one-paragraph design note on how you'd generalize the routing (feeds L4.1).
