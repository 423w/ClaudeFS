# Branch 0: Orientation

**Branch goal:** know what you're building, why, and with what. Leave this branch with trained ears
(a little), the math you need for Branch 1, a working build that runs a test and writes a file, and
a first decision about which kind of synth FSS will be.

Every lesson uses the same template:
**Why** · **Concepts** · **C++ focus** · **Architecture / code shape** · **Build steps** · **[THINK]** prompts ·
**[DECISION]** gates · **Experiments** · **Resources** · **Exit criteria** · **Stretch**

---

## L0.1: Listening & vocabulary: what a synth is

**Why.** You'll make better engineering choices if you know what musicians reach for. A synth is a
*signal path* (things that make and shape sound) plus a *control path* (things that change those
shapes over time). Everything we build is one or the other.

**Concepts**
- Sources: oscillators (sine, saw, square, triangle, noise), and what "brightness" means (harmonics).
- Shapers: filters (low-pass, high-pass, band-pass, cutoff, resonance), amplifier (VCA), waveshapers.
- Modulators: envelopes (ADSR), LFOs, velocity, key tracking, mod wheel. "Modulation" means one signal controlling a parameter.
- Voices: mono vs poly, voice count, unison.
- The three classic "families": subtractive (bright source → filter), FM (oscillators modulating oscillators), and additive (sum of sines).

**Build steps** (no code yet)
1. Do all of `[LEARNING-SYNTHS]` (about an hour, playful).
2. Read `[SYNTH-SECRETS]` parts 1–4 (what sound is, harmonics, waveforms).
3. Install **Vital**, **Surge XT**, and **Dexed**. Load an init patch in Vital and make a pluck, a pad,
   and a bass *without presets*.
4. In Dexed, load a few factory sounds (e-piano, bell, bass) and look at the algorithm diagram. Turn
   one operator's output level up and down. Just notice what happens; no understanding is required yet.
5. Start your journal: `docs/journal/L0.1.md`.

**[THINK]**
- When you turned the filter cutoff down, which part of the sound disappeared first?
- Why does a pad feel "slow"? Which knob made it slow?
- In Dexed, some operators change the *tone* and others change the *loudness*. Why might that be?

**Experiments.** Pick 3 sounds from songs you love. For each, describe in synth vocabulary what you
think is happening (source? filter moving? envelope shape?). You'll revisit this list at L4.7 and see how much your ear has improved.

**Exit criteria**
- [ ] You can explain oscillator, filter, envelope, LFO, voice, and modulation in your own words.
- [ ] You made 3 sounds from init patches and wrote down how.
- [ ] You can sketch a signal-flow diagram of one Vital patch.

---

## L0.2: Just-enough math

**Why.** DSP is applied math, but you need far less than you might fear to *start*. Learn these
now; deeper math (complex numbers, z-transforms) arrives just in time in L1.4 and L1.9.

**Concepts**
- **Sinusoids:** `x(t) = A·sin(2π·f·t + φ)`. What amplitude, frequency, and phase mean, and why radians.
- **Phase as a rotating angle**, and frequency as *how fast phase advances*. (This one idea powers L1.2 and all of FM.)
- **Sampling:** `t = n / fs`. Nyquist frequency `fs/2`. **Aliasing:** frequencies above Nyquist fold back down.
- **Logs and exponentials:** pitch is exponential (`f = 440 · 2^((note − 69)/12)`), loudness perception
  is logarithmic (`dB = 20·log10(amplitude)`), and a cent is 1/100 of a semitone.
- **Harmonics:** a periodic wave is a sum of sines at integer multiples of its fundamental (Fourier's big idea).
- **Complex numbers (preview):** `e^{iθ} = cos θ + i sin θ` as a rotating arrow. Just meet it now.

**C++ focus.** A tiny `fss/util/math.h` with `constexpr` helpers: `midiToHz`, `hzToMidi`,
`dbToGain`, `gainToDb`, `centsToRatio`. Use `std::numbers::pi_v<float>`.

**Build steps**
1. Watch `[3B1B-FOURIER]`. Read `[DSPGUIDE]` ch. 1–3 (and skim ch. 8 on the DFT), and `[PUCKETTE]` ch. 1.
2. By hand: what's the frequency of MIDI note 60? Of 69 + 7 semitones? What gain is −6 dB? −12 dB? −60 dB?
3. Write the helpers above (once L0.3's build works) with unit tests: `midiToHz(69) == 440`,
   `dbToGain(-6.0206f) ≈ 0.5`, and round-trip tests.

**[THINK]**
- If `fs = 48000` and you generate a 30 kHz sine, what frequency do you hear? (Answer: 18 kHz. Why?)
- Why do synth knobs for cutoff and envelope time usually feel "logarithmic"?
- A saw wave at 5 kHz contains harmonics at 10, 15, 20, 25 kHz... At 48 kHz sample rate, where do they go?

**Exit criteria**
- [ ] You can convert MIDI ↔ Hz, gain ↔ dB, and semitones/cents ↔ ratio without looking it up.
- [ ] You can explain aliasing with a drawing.
- [ ] The math helpers exist, with passing tests.

---

## L0.3: Toolchain & repo skeleton

**Why.** A fast build-run-listen-plot loop is the engine of learning here. Set it up once, well.

**Concepts.** CMake targets (library vs executable), include paths, `FetchContent` for dependencies,
build types (Debug vs Release, which matters a lot for DSP speed), warnings as teachers, unit tests, and
`.gitignore` for build output and renders.

**C++ focus.** Modern CMake (`target_*` commands, never global flags), `CMakePresets.json`, C++20,
MSVC `/W4 /permissive-` (or clang-cl `-Wall -Wextra`), a header-only-first library with an `INTERFACE` or static target.

**Architecture / code shape** (see [ARCHITECTURE.md](../ARCHITECTURE.md) §2)
```
CMakeLists.txt          → add_subdirectory(core) (tools/render) (tests)
core/                   → add_library(fss_core ...) ; target_compile_features(cxx_std_20)
tools/render/main.cpp   → prints "hello", later renders WAVs
tools/analyze/*.py      → plot_wave.py, plot_spectrum.py, spectrogram.py
tests/                  → test_math.cpp
renders/                → git-ignored output
```

**Build steps**
1. Install VS 2022 Build Tools (C++ workload), CMake, Ninja, Git, and Python 3 with `numpy scipy matplotlib`.
2. **[DECISION D3, D4]** Pick the compiler and test framework (see [DECISIONS.md](../DECISIONS.md)).
3. Create the skeleton. A Debug and a Release preset. One test that passes.
4. Write `tools/analyze/plot_spectrum.py`: load a WAV (`scipy.io.wavfile`), apply a window
   (Hann), FFT, and plot magnitude in dB on a log-frequency axis. You'll use it constantly. (Writing it
   with Claude's help is fine; it's a tool, not the lesson. Do understand *why* the window matters, though; L3.6 goes deeper.)
5. Write `spectrogram.py` too (`scipy.signal.spectrogram`). It's the best aliasing detector there is.

**[THINK]** Why should `fss_core` be its own library target rather than files compiled into each app?

**Exit criteria**
- [ ] `cmake --preset release && cmake --build --preset release && ctest` works from a clean clone.
- [ ] Analysis scripts plot a WAV you got from anywhere.
- [ ] L0.2's math tests pass.

**Stretch.** A GitHub Actions workflow that builds and tests on Windows (L6.1 expands on this).

---

## L0.4: Choose your synth (Decision gate D1, D2)

**Why.** The synth type shapes everything after Branch 1's basics. You should choose it from
experience, not from a recommendation alone. (Your README already says "FM", so test that instinct.)

**Concepts.** Read the D1 table in [DECISIONS.md](../DECISIONS.md). Key ideas:
- **Subtractive:** start harmonically rich, then carve away with a filter. Intuitive; the DSP challenges are anti-aliasing and filters.
- **FM/PM:** an oscillator's *phase* is pushed back and forth by another oscillator. This creates sidebands, which are new
  frequencies. Rich spectra from just sines. Sound design is less intuitive because the spectrum changes non-monotonically with the index.
- **Hybrid:** an FM source into a subtractive filter. You get FM's richness and the filter's intuitive control. Ableton
  Operator, Elektron Digitone, and Korg opsix all do this.

**Build steps**
1. In Dexed or Ableton Operator, make a **2-operator** patch: one carrier, one modulator.
   Sweep the modulator's level (that's the *index*). Then change the ratio: 1, 2, 3, 3.5, 1.41.
   Write down what you hear for integer vs non-integer ratios.
2. In Vital, make a classic subtractive bass: saw → low-pass with a filter envelope.
3. Read Chowning's paper intro `[CHOWNING73]`. Skim `[OPERATOR-MANUAL]`.
4. **[DECISION D1]** Write your reasoning in DECISIONS.md. Status becomes `LEANING`; you'll confirm it after L1.5.
5. **[DECISION D2]** Record which DAW(s) you use. This picks the plugin path in Branch 3.

**[THINK]**
- Which of the two patches did you *enjoy* tweaking more? That matters as much as anything technical.
- What sounds do you want to make in your music? Bells, keys, and metallic basses favor FM; warm pads and acid basses favor subtractive.
- Do the first lessons of Branch 1 depend on this choice? (Mostly no. Oscillators and envelopes are universal. That's why the gate can stay soft until L1.5.)

**Exit criteria**
- [ ] D1 recorded as `LEANING`, with reasons. D2 recorded.
- [ ] You can describe in one paragraph what FSS v1 will be.
