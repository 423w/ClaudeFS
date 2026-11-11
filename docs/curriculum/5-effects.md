# Branch 5: Effects Engine

**Branch goal:** a built-in, global stereo FX chain (drive, chorus, delay, reverb, EQ, limiter) inside
`fss::fx`, written from scratch. Effects reuse everything you've learned (filters, oscillators as LFOs,
smoothing) and add one big new primitive: the **delay line**.

Suggested order: L5.1 → L5.3 → L5.4 → L5.2 → L5.5 → L5.6. Chorus arrives early because FM e-pianos and chorus are a classic pairing.

---

## L5.1: FX architecture: the chain, dry/wet, tails

**Why.** Decide how effects plug together *before* writing five of them.

**Concepts**
- Insert (serial) vs send (parallel) effects. In a synth, the FX are usually a serial chain after the voice sum.
- Dry/wet mixing: linear vs equal-power crossfade. "Wet only" on reverbs used as sends.
- Tails: delays and reverbs keep sounding after the notes stop. The host needs to know (CLAP `tail` extension), and bypass must not cut tails abruptly.
- Smooth bypass and smooth reordering (crossfade), per-effect enable, latency reporting (oversampled effects add latency).
- Stereo in/out everywhere, even for mono algorithms.

**C++ focus.** The interface choice (D16): a virtual `Effect` base (simple; one virtual call per *block* is cheap) vs
`std::variant<Drive, Chorus, ...>` with `std::visit` (no heap, value semantics) vs a fixed struct of all effects
(simplest, fixed order). Measure, don't guess.

**Architecture / code shape**
```cpp
namespace fss::fx {
struct StereoBlock { float* L; float* R; int n; };
class Effect {                          // if you choose virtual
public:
    virtual ~Effect() = default;
    virtual void prepare(double sr, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process(StereoBlock) = 0;
};
class FxChain { /* fixed array of slots, enable flags, mix smoothing */ };
}
```

**Build steps**
1. **[DECISION D16]**
2. Implement the chain with two trivial effects: **gain** (dB, smoothed) and **stereo width** (mid/side).
3. Bypass and enable toggles while playing must be click-free.

**Exit criteria**
- [ ] Chain framework plus two effects, with click-free toggling (tested by render).

---

## L5.2: Gain, saturation, distortion, bitcrush & oversampling

**Why.** The simplest effects to write and the hardest to make sound good. They're also where oversampling stops being optional.

**Concepts**
- Reuse the L4.5 shapers. Pre- and post-EQ (a "tone" control: filter before and after the shaper changes the character completely).
- **Oversampling:** upsample (zero-stuff + low-pass) → nonlinearity → low-pass → downsample. Halfband filters (FIR polyphase
  or IIR allpass-based) and their latency vs quality trade-off. Report the latency to the host.
- **Bitcrusher:** quantize amplitude (bits) and **sample-rate reduction** (sample-and-hold), which is *deliberate* aliasing. Ironic and fun.
- Output gain compensation and the "louder sounds better" trap when A/B testing.

**Build steps**
1. Distortion effect with 1×/2×/4× oversampling. Measure the alias reduction (sine sweep → spectrogram).
2. Bitcrusher. Use it on purpose on an FM bell.
3. Loudness-matched A/B.

**Exit criteria**
- [ ] Measured aliasing at 1×, 2×, and 4× in your journal.
- [ ] Latency is reported correctly when oversampling is on.

---

## L5.3: Delay lines & a tempo-synced delay

**Why.** The delay line is the most reused structure in effects (chorus, flanger, reverb, physical models), and it's simple to write.

**Concepts** (`[JOS-PASP]` delay lines, `[DATTORRO]` part 2)
- Circular buffer with a write index. Read at `write − delay`. Power-of-two size with mask indexing.
- **Fractional delay:** linear interpolation (cheap, slight low-pass), allpass interpolation (flat magnitude, bad for modulation), cubic/Lagrange.
- Feedback: echoes; a filter in the feedback loop gives darker repeats (tape/analog character); saturation in the loop.
- Ping-pong (cross-feedback L↔R). Tempo sync from host transport (note values → ms).
- Changing delay time smoothly: smoothing the time gives pitch-shift artifacts (the "tape" sound), crossfading between two taps gives a clean change. Both are valid choices.

**C++ focus.** A reusable `DelayLine` template (sample type, interpolation policy as a template parameter), all allocation in
`prepare()` (max delay time), and `std::span`.

**Build steps**
1. A `DelayLine` primitive with a fractional read. Test: an impulse in comes out at the exact (fractional) position.
2. A stereo delay effect: time (ms or sync), feedback, ping-pong, damping filter, mix.
3. Map the host tempo (CLAP transport / your standalone's own BPM).

**Exit criteria**
- [ ] A tempo-synced ping-pong delay with no clicks when changing time while playing (with your chosen behavior documented).

---

## L5.4: Modulated delays: chorus, flanger, phaser

**Why.** Short modulated delays create movement and width, and the DX7 e-piano + chorus pairing is a classic.

**Concepts**
- **Chorus:** delay ~10–30 ms, LFO-modulated by a few ms, multiple voices, L/R LFO offset. Juno-style simplicity vs lush multi-voice.
- **Flanger:** delay ~0.5–10 ms with feedback, which gives a comb filter sweep. Through-zero flanging (stretch).
- **Phaser:** not a delay! A chain of first-order **allpass** filters with modulated break frequencies, mixed with dry, gives notches.
  Revisit L1.9's transfer functions: an allpass has flat magnitude but frequency-dependent phase.
- Interpolation quality matters now: modulated reads expose linear interpolation's flaws.

**Build steps**
1. Chorus from your `DelayLine` + `Lfo`. Put it on the e-piano patch.
2. Flanger (add feedback, shorten the delay). Look at the comb in a spectrum.
3. Phaser with 4–12 allpass stages.

**Exit criteria**
- [ ] All three effects, with the FM e-piano + chorus sounding classic.
- [ ] You can explain why a phaser isn't a flanger.

---

## L5.5: Reverb: Schroeder → Freeverb → FDN/Dattorro

**Why.** Reverb is where DSP becomes art, and where linear algebra (mixing matrices) appears in audio. It's a deep, rewarding rabbit hole.

**Concepts**
- Physical picture: direct sound, early reflections, the late diffuse tail, and RT60 (time to decay by 60 dB).
- **Comb filters** (feedback delay) and **Schroeder allpasses** (diffusers). Schroeder's design (parallel combs → series allpasses).
- **Freeverb:** 8 damped combs + 4 allpasses per channel. Simple, recognizable, a bit metallic.
- **Dattorro plate** `[DATTORRO]`: input diffusion → a figure-eight "tank" of modulated allpasses and delays, with output taps. Gorgeous.
- **FDN** `[SIGNALSMITH-REVERB]`, `[JOS-PASP]`: N delay lines mixed through an orthogonal matrix (Householder, Hadamard) for energy-preserving
  feedback. Echo density, mutually prime delay lengths, decay per line set from RT60: `g = 10^(−3·delay/RT60)`.
- Modulation (slight delay wobble) breaks up metallic resonances. Damping (low-pass in the loop) makes highs decay faster, like real rooms.

**C++ focus.** Fixed-size matrix operations (`std::array`), a Hadamard transform done recursively without multiplies,
and template metaprogramming for N (stretch).

**Build steps**
1. Read `[VALHALLA]`'s "Getting Started With Reverb Design" and `[SIGNALSMITH-REVERB]`.
2. Build Schroeder, then Freeverb. Listen to their flaws.
3. **[DECISION D17]** Then build the "real" one (an FDN with diffusion, or Dattorro).
4. Measure: render an impulse response, compute RT60 from the energy decay (Schroeder backward integration in Python), and compare it to the knob setting.

**Exit criteria**
- [ ] Measured RT60 within ~10% of the setting.
- [ ] A reverb you'd use on your FM bells instead of your DAW's stock reverb.

---

## L5.6: EQ & dynamics: parametric EQ, compressor, output limiter

**Why.** Finishing tools. The output limiter is also a *safety* feature (feedback runaway, resonance spikes).

**Concepts**
- Parametric EQ from `[RBJ]` biquads (or SVF-based bells/shelves): low shelf, peaks, high shelf, with smoothed coefficient changes.
- Compressor `[GIANNOULIS-COMP]`: level detection (peak vs RMS), gain computer in dB (threshold, ratio, knee),
  attack/release smoothing *in the log domain*, makeup gain. Feed-forward vs feedback topology.
- Limiter: fast attack, lookahead (adds latency, so report it), true-peak (oversampled) detection as a stretch.

**Build steps**
1. A 3-band EQ.
2. A compressor. Plot its static curve (input dB vs output dB) and its step response.
3. An output safety limiter (always on, ceiling at −0.3 dBFS).

**Exit criteria**
- [ ] The compressor static curve plot matches theory.
- [ ] An extreme resonance or feedback patch can't exceed the ceiling.
