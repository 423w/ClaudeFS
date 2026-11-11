# Branch 6: Craft & Mastery (cross-cutting)

**Branch goal:** the professional habits that make DSP code trustworthy, fast, and shippable. Unlike the
other branches, you **pull these lessons in when you need them**. Each one lists its trigger.

---

## L6.1: Testing DSP: golden files, properties, null tests, CI

**Trigger:** start the basics in L0.3, and expand at L1.7 (block-size invariance) and before any refactor.

**Concepts**
- **Unit tests** for primitives: known inputs, analytically known outputs (a one-pole step response, a biquad at DC and Nyquist).
- **Property tests:** for random parameters and inputs, the output is finite (no NaN/Inf), bounded, silence in gives silence out
  for effects, and *block-size invariant*.
- **Golden files:** render reference WAVs, then compare future renders with a tolerance. Bless new goldens deliberately when you
  *intend* a sound change. Store small goldens, or store hashes plus metrics.
- **Null tests:** subtract two renders. If the result is silent, they're identical. Great for refactors (e.g. L4.1, L6.2).
- **Spectral assertions:** "the energy above X kHz is below −Y dB" (aliasing regression tests).
- CI on GitHub Actions (Windows runner): build, test, and upload renders as artifacts.

**C++ focus.** Test fixtures, parameterized tests, deterministic random seeds, `ctest` labels (fast vs slow).

**Exit criteria**
- [ ] Every primitive has tests, and the golden/null-test harness exists.
- [ ] CI is green on each push.

---

## L6.2: Performance: data-oriented design & SIMD voices

**Trigger:** after M4/M5, when you want 32+ voices with unison, or when profiling shows voices dominate.

**Concepts**
- Data-oriented design: struct-of-arrays (SoA) vs array-of-structs (AoS), cache lines, hot/cold splitting. Watch Mike Acton's
  "Data-Oriented Design and C++" (CppCon 2014).
- **SIMD:** process 4 (SSE) or 8 (AVX) voices *in lockstep*, one voice per lane. That's how Vital and Surge get their polyphony.
  Branches become masks/blends. Intrinsics (`_mm_add_ps`) vs wrappers (xsimd, or your own tiny `float4`).
- Block processing: per-sample virtual calls and branches out of inner loops. Loop vectorization reports (`/Qvec-report:2` in MSVC).
- Fast math approximations (tanh, exp2, sin) with *measured* error budgets. `-ffast-math` / `/fp:fast` hazards.

**C++ focus.** `alignas`, intrinsics, writing a `float4` wrapper with operator overloading, benchmarks (nanobench or
Google Benchmark), and reading compiler output (Compiler Explorer, godbolt.org).

**Build steps.** Profile → pick the voice inner loop → write a 4-wide SoA version → null-test it against the scalar version → measure.

**Exit criteria**
- [ ] A measured speedup (target 2–3×) with null-test equivalence (within float tolerance).

---

## L6.3: Oversampling & anti-aliasing filter design

**Trigger:** L4.2, L4.5, or L5.2, when simple oversampling isn't good enough or costs too much latency or CPU.

**Concepts**
- Up- and downsampling theory, images vs aliases. Why the filter is the whole game.
- FIR design: windowed sinc (Kaiser window and its β/length formulas), the equiripple idea, linear vs minimum phase (latency vs pre-ringing).
- **Halfband filters** (every other coefficient zero) and polyphase implementation. Cascading 2× stages for 4× and 8×.
- IIR alternatives: polyphase allpass halfbands (very cheap, nonlinear phase).

**Build steps.** Design a halfband FIR in Python (scipy) *and* in C++, verify the stopband attenuation by measurement, and build a
reusable `Oversampler<Factor>`.

**Exit criteria**
- [ ] An oversampler with documented passband ripple, stopband attenuation, and latency, verified by measurement.

---

## L6.4: Robustness: sample rates, block sizes, NaNs, pluginval

**Trigger:** before using FSS on real projects (L3.3), and before any release.

**Checklist**
- Sample rates 44.1 / 48 / 88.2 / 96 / 192 kHz: envelope times, filter cutoffs, delay times, and LFO rates must all be identical by ear and by measurement.
- Block sizes 1 to 8192, variable per call, and 0. Events at frame 0 and at the last frame.
- Parameter extremes: every param at min and at max simultaneously (fuzz random patches for an hour; output stays finite).
- NaN/Inf guards: detect, reset the voice or filter, and log it (via the queue). Never let a NaN reach the host.
- Lifecycle abuse: activate/deactivate loops, state load during playback, multiple instances.
- `[PLUGINVAL]` at strictness 10. `[CLAP-VALIDATOR]`.

**Exit criteria**
- [ ] A fuzz harness exists and runs in CI (short) or nightly (long).
- [ ] pluginval passes at strictness 10.

---

## L6.5: Shipping: versioning, licensing, installers

**Trigger:** when you want friends (or the world) to use FSS.

**Concepts**
- Semantic versioning, plugin IDs that never change, preset compatibility promises.
- **[DECISION D18]** License: what you borrowed decides some of this (GPL vs permissive; JUCE terms). Choose consciously.
- Packaging: the CLAP and VST3 install paths on Windows (`C:\Program Files\Common Files\CLAP` and `...\VST3`), an installer (Inno Setup / WiX), and code signing (optional, which reduces scary warnings).
- A manual: even one page. Demo audio.

**Exit criteria**
- [ ] A tagged release that a friend installs and plays.

---

## L6.6: Making music with it (the real point)

**Trigger:** always.

- Use FSS on every track you can, even when another synth would be faster. Keep the friction log from L4.7 going.
- Every few months, reread your L0.1 journal. Notice how your ears and code have changed.
- Share renders, with a friend, with the Audio Programmer community, wherever. Explaining what you built is the last step of learning it.
