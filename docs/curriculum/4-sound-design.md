# Branch 4: Sound Design Depth

**Branch goal:** turn a working synth into an *instrument* you reach for. That means deeper modulation, more
source material, width, grit, and a preset bank. Lessons here are fairly independent; follow your curiosity.

---

## L4.1: The modulation matrix & polyphonic modulation

**Why.** Hardwired routes (L1.11) run out fast. A mod matrix lets any source modulate any destination,
and it's where modern synths get their life.

**Concepts**
- **Sources:** envelopes (amp, filter, plus 1–2 extra mod envelopes), LFOs (per voice and global), velocity,
  key track, mod wheel, aftertouch, pitch bend, random per note, constant, and maybe MPE dimensions.
- **Destinations:** any continuous param (op ratio fine, op level, feedback, cutoff, resonance, pan, LFO rate...).
- A slot = (source, destination, amount, optionally a via/scale source). Amounts in the destination's natural units (semitones, octaves, dB).
- **Per-voice vs global** modulation: a per-voice source can't modulate a global (post-mix) destination. Why?
- **Polyphonic modulation from the host (CLAP `PARAM_MOD` with note IDs):** automation that's different per note. Only CLAP has this.
- Evaluation cost: compute modulated values at control rate (e.g. every 16–32 samples) with smoothing, or every sample for audio-rate destinations.

**C++ focus.** Data-oriented layout (arrays of source values, arrays of slots), avoiding virtual calls per sample,
`enum class` indices with `std::to_underlying`, and generating the UI and serialization from the same tables.

**Architecture / code shape**
```cpp
enum class ModSrc : std::uint8_t { Env1, Env2, Lfo1, Lfo2, Velocity, KeyTrack, ModWheel, Aftertouch, Random, Count };
struct ModSlot { ModSrc src; ParamId dst; float amount; ModSrc via = ModSrc::Count; };
struct VoiceModState { std::array<float, size_t(ModSrc::Count)> src; /* per-voice */ };
// per block: fill src[] → for each slot: offsets[dst] += src * amount → apply offsets at param read time
```

**Build steps**
1. Design: list your sources and destinations, decide the slot count (8? 16?), and decide control-rate vs audio-rate.
2. Refactor L1.11's hardwired routes into matrix slots (the behavior must stay identical; golden-file test!).
3. Add the matrix to the GUI. Then CLAP poly-mod support (stretch).

**[THINK]** How does FM index modulation by an LFO differ from modulating carrier level? Which destinations need audio-rate updates?

**Exit criteria**
- [ ] Old patches sound identical after the refactor (golden test).
- [ ] A patch where 4+ mod routes create an evolving sound.

---

## L4.2: Richer operators: waveforms, ratios, FM aliasing

**Why.** This is where the "hybrid" grows. Non-sine operator waveforms widen the palette enormously (TX81Z-style
waveforms, or saw/square from L1.8, which give you a VA oscillator inside each operator).

**Concepts**
- TX81Z-style waveforms (half-sine, abs-sine, quarter-sines, and so on) are cheap variants of the sine table.
- Using PolyBLEP saw/square as *carriers* turns FSS into a subtractive synth. Using them as *modulators* gets complicated (their many harmonics each create sidebands).
- **FM aliasing:** high index × high modulator frequency puts sidebands above Nyquist, and they fold back.
  Mitigations: key-level scaling (L1.6), index limiting per note, and **oversampling** the operator section (2×/4×; L6.3 goes deep).
- Ratio quantization UX: snapping to harmonic ratios vs free; a "fixed" mode for percussive sounds.

**Build steps**
1. Add waveform selection per operator. Render a spectrum of each waveform as a carrier, and as a modulator at I=1.
2. Measure aliasing for a bright FM patch across the keyboard. Add 2× oversampling of the voice (a simple halfband
   downsampler is fine for now) and compare.
3. Make an all-saw-carrier patch through the filter and confirm FSS can do classic subtractive sounds.

**Exit criteria**
- [ ] Measured aliasing reduction from oversampling, recorded in your journal.
- [ ] Classic VA-style and TX81Z-style patches saved.

---

## L4.3: Wavetable oscillators & mipmapping

**Why.** Wavetables give arbitrary single-cycle timbres and smooth morphing. Making them alias-free teaches
interpolation, mipmapping, and practical FFT use.

**Concepts** (`[EARLEVEL]` wavetable oscillator series, `[PUCKETTE]` wavetable chapters)
- A table of one cycle (e.g. 2048 samples); reading it at fractional positions; interpolation (linear, cubic Hermite) and its error.
- **Mipmapping:** precompute band-limited versions (one per octave) by zeroing harmonics above Nyquist via FFT → zero → iFFT
  (reuse your L3.6 FFT!), and select a table by playback frequency. Crossfade between levels.
- Frames and **morphing** (scanning through 64–256 frames), loading single-cycle WAVs (your L1.1 reader).
- Where it fits in FSS: a wavetable as an operator waveform (wavetable FM!) or as a separate oscillator.

**C++ focus.** Memory layout (contiguous frames × mip levels), `prepare()`-time generation (non-RT), atomically swapping a
newly loaded table into the audio thread (RCU-like pointer swap; who frees the old one?).

**Build steps**
1. Build a table generator (additive or from a WAV), the mip levels, and an interpolating oscillator.
2. Sweep a bright table across the keyboard and verify no aliasing in the spectrogram.
3. Implement table loading from the UI, with a *safe* hand-off to the audio thread.

**Exit criteria**
- [ ] An alias-free wavetable sweep.
- [ ] Table loading while playing causes no glitches or crashes.

---

## L4.4: Unison, detune & stereo

**Why.** Width and thickness. Supersaws, lush pads, and wide FM leads all come from here.

**Concepts**
- Unison: N copies of the voice's oscillators (or of the whole voice), detuned (cents spread), with
  phase randomization and stereo panning. CPU scales with N.
- Detune spread curves (linear vs JP-8000-style nonlinear), and blending the center voice vs the side voices.
- Pan laws: constant-power (`cos/sin`) vs linear. Stereo width. Mono compatibility (sum to mono and listen!).
- Per-voice panning by key or by voice index. Stereo LFOs (phase offset L/R).

**Build steps**
1. Unison in the voice with 1–8 copies. Measure the CPU cost.
2. Supersaw (saw carriers, from L4.2) and a wide FM bell.
3. A mono compatibility check: render L+R and listen for phasey cancellation.

**Exit criteria**
- [ ] A wide pad that survives mono summing.
- [ ] Unison count is changeable while playing without clicks.

---

## L4.5: Nonlinearity in the voice: drive, waveshaping, ADAA

**Why.** Grit and warmth. Nonlinearities create new harmonics, and they also create *aliasing*, which leads to
one of the neatest modern DSP tricks.

**Concepts**
- Waveshaping: `y = f(x)` (tanh, soft clip polynomials, hard clip, foldback). Odd functions give odd harmonics; asymmetry gives even harmonics (and DC → DC blocker).
- Drive = gain into the shaper; output compensation.
- Aliasing from nonlinearity: polynomial degree × bandwidth. Remedies: oversampling, or **ADAA** `[PARKER-ADAA]`,
  which uses the antiderivative of `f` to compute a band-limited-ish output (first-order: `(F(x[n]) − F(x[n−1]))/(x[n] − x[n−1])`, with care when the difference is ~0).
- Placement: pre-filter drive vs post-filter drive vs in the filter feedback loop (the ladder from L1.10) sound very different.

**Build steps**
1. Implement shapers, render a sine through each, and plot the harmonics.
2. Implement first-order ADAA for tanh and hard clip, and compare aliasing vs naive vs 4× oversampling (render, spectrogram, measure).
3. Add a drive stage to the voice, with selectable position.

**Exit criteria**
- [ ] A three-way aliasing comparison in your journal.
- [ ] A driven FM bass you'd use in a track.

---

## L4.6: Detours: Karplus-Strong, additive, noise (play!)

**Why.** This is the "come back out of love" lesson. Small, delightful side quests that teach new ideas and may become features.

Pick any:
- **Karplus-Strong:** a noise burst into a delay line with a low-pass in the feedback gives a plucked string (`[JOS-PASP]`).
  It previews Branch 5's delay lines. Could be an "operator type"!
- **Additive oscillator:** 64 sines with per-partial envelopes. Resynthesize a recorded sound with your FFT.
- **Noise:** white, pink (Voss-McCartney or filtering), and noise as an FM modulator. Breath and percussion.
- **Formant FM:** carriers at formant frequencies give vowel-like tones (Chowning's singing voice work).
- **Multi-segment envelopes** (D6 revisited), drawn in the GUI.
- **Arpeggiator** (needs host tempo).

**Exit criteria.** You had fun, and you wrote a journal entry about one thing that surprised you.

---

## L4.7: Sound design sprint: a factory preset bank ★ Milestone M6

**Why.** You'll learn more about what FSS needs by making 16–32 real sounds than by any lesson. Expect to discover
missing features, bad ranges, and awkward UI. That's the point.

**Build steps**
1. Revisit the three "sounds I love" from L0.1. Recreate them in FSS.
2. Make a bank: 4 basses, 4 keys/e-pianos, 4 bells/mallets, 4 pads, 4 leads, 4 plucks, 4 FX/percussion.
   Use `[WELSH]` and `[FM-THEORY-APPS]` as references.
3. Keep a *friction log* (every time you thought "I wish...") and turn it into future lessons or tasks.

**Exit criteria (M6)**
- [ ] A shipped factory bank.
- [ ] FSS used on a finished track.
- [ ] The friction log is triaged into new tasks.
