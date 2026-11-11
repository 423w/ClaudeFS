# Resources

An annotated library. Lessons cite these by **ID** in brackets. **Free** means legally free online.
💰 means you'd buy it (or find it in a library or O'Reilly subscription). ⭐ marks the core texts. If you only
get a few things, get those.

> Some links will rot. If one dies, search the title. Most of these are famous enough to resurface.

---

## A. Synthesis: understanding sound & synths (ears first)

| ID | Resource | Why / when |
|----|----------|-----------|
| `[LEARNING-SYNTHS]` | **Ableton: Learning Synths**, learningsynths.ableton.com. Free | Interactive browser playground for oscillators, filters, envelopes, and LFOs. L0.1 |
| `[SYNTH-SECRETS]` ⭐ | **Gordon Reid: *Synth Secrets*** (Sound On Sound, 63 parts, 1999–2004), soundonsound.com/series/synth-secrets-sound-sound. Free | The classic. Explains *why* synth sounds are what they are, from physics to patches. Read it alongside the whole project |
| `[WELSH]` | **Fred Welsh: *Welsh's Synthesizer Cookbook*** 💰 | Harmonic spectra of real instruments, plus patch recipes. L4.7 |
| `[SYNTORIAL]` | **Syntorial** (ear-training app) 💰, free demo | Trains you to hear what a parameter does. Great while building your sound-design ear |
| `[FM-THEORY-APPS]` | **Chowning & Bristow: *FM Theory and Applications: By Musicians for Musicians*** (Yamaha, 1986). 💰 (out of print; used copies or library) | FM explained for musicians by its inventor. L1.4–L1.5, L4.7 |
| `[OPERATOR-MANUAL]` | **Ableton Live manual: Operator chapter**. Free | Clear, practical description of a modern 4-op FM + filter synth, which is basically FSS's ancestor |
| `[DIGITONE-MANUAL]` | **Elektron Digitone manual**. Free PDF | Another 4-op FM + filter design. Compare its algorithm and ratio choices to your own |

## B. DSP foundations

| ID | Resource | Why / when |
|----|----------|-----------|
| `[DSPGUIDE]` ⭐ | **Steven W. Smith: *The Scientist and Engineer's Guide to DSP***, dspguide.com. Free | The most readable DSP intro. Sampling, convolution, Fourier, and filters with minimal math. L0.2, L1.9 |
| `[PUCKETTE]` ⭐ | **Miller Puckette: *The Theory and Technique of Electronic Music***, msp.ucsd.edu/techniques.htm. Free | Written by the author of Max/Pd, and aimed squarely at music. Covers sinusoids, wavetables, modulation (FM!), delays, filters, and classic waveforms |
| `[JOS-MDFT]` | **Julius O. Smith III: *Mathematics of the DFT***, ccrma.stanford.edu/~jos/mdft/. Free | Complex numbers, sinusoids, and the DFT done properly. L0.2, L3.6 |
| `[JOS-FILTERS]` ⭐ | **J.O. Smith: *Introduction to Digital Filters with Audio Applications***, ccrma.stanford.edu/~jos/filters/. Free | Difference equations, transfer functions, poles/zeros, biquads. L1.9 |
| `[JOS-PASP]` | **J.O. Smith: *Physical Audio Signal Processing***, ccrma.stanford.edu/~jos/pasp/. Free | Delay lines, interpolation, comb/allpass, reverb, FDNs, Karplus-Strong. Branch 5, L4.6 |
| `[JOS-SASP]` | **J.O. Smith: *Spectral Audio Signal Processing***, ccrma.stanford.edu/~jos/sasp/. Free | Windows, FFT analysis, and spectrum display done right. L3.6 |
| `[LYONS]` | **Richard Lyons: *Understanding Digital Signal Processing*** (3rd ed.) 💰 | Excellent "explain it like an engineer" second textbook, if you want one |
| `[3B1B-FOURIER]` | **3Blue1Brown: "But what is the Fourier Transform? A visual introduction"** (YouTube). Free | Builds the intuition before the math. L0.2 |
| `[ROADS]` | **Curtis Roads: *The Computer Music Tutorial*, 2nd ed.** (MIT Press, 2023) 💰 | The encyclopedia (1000+ pages). Look things up in it; don't read it cover to cover |

## C. Synth DSP: the specific techniques

| ID | Resource | Why / when |
|----|----------|-----------|
| `[PIRKLE-SYNTH]` ⭐ | **Will Pirkle: *Designing Software Synthesizer Plugins in C++*, 2nd ed.** (Focal/Routledge, 2021) 💰, plus the **SynthLab** code (github.com/willpirkleaudio) | The closest thing to "this project, as a textbook": oscillators, envelopes, VA filters, FM, wavetables, voice architecture, and modulation in C++. Use it as a reference and a second opinion, not a recipe to copy |
| `[PIRKLE-FX]` | **Will Pirkle: *Designing Audio Effect Plugins in C++*, 2nd ed.** (2019) 💰 | The FX companion. Biquads, delays, reverbs, dynamics, oversampling. Branch 5 |
| `[CHOWNING73]` ⭐ | **John Chowning: "The Synthesis of Complex Audio Spectra by Means of Frequency Modulation"** (JAES 21(7), 1973). Free PDF copies (search title; e.g. Yamaha and UMich hosts) | The paper that started FM. Short and readable, with the bell, brass, and woodwind recipes. L1.4 |
| `[SHIRRIFF-DX7]` | **Ken Shirriff: Yamaha DX7 reverse-engineering series** (righto.com, 2021–22). Free | How the DX7's chip actually computes FM: log-sine ROM, exponential table, phase accumulators, algorithms, feedback. Fascinating for L1.2 and L1.5 |
| `[MSFA]` | **Raph Levien et al.: Music Synthesizer for Android (msfa)**, github.com/google/music-synthesizer-for-android (Apache 2.0) | A real DX7-compatible FM engine in C++. Read `fm_core`, `fm_op_kernel`, and `env`. Dexed is built on it |
| `[DEXED]` | **Dexed** (github.com/asb2m10/dexed, GPLv3) | A free DX7 plugin. *Use it as an instrument* to learn FM before and alongside building yours |
| `[ZAV]` ⭐ | **Vadim Zavalishin: *The Art of VA Filter Design*** (Native Instruments, rev. 2.1.x). Free PDF | The bible of modern virtual-analog filters: TPT/ZDF, the SVF, the ladder, nonlinearities. L1.10. Hard but worth it; read the 1-pole and time-discretization chapters first |
| `[SIMPER-SVF]` | **Andrew Simper (Cytomic): "Linear Trapezoidal Integrated State Variable Filter With Low Noise Optimisation"**, cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf. Free | The SVF that much of the industry actually ships, with ready-to-code equations. L1.10 |
| `[RBJ]` ⭐ | **Robert Bristow-Johnson: Audio EQ Cookbook**, w3.org/TR/audio-eq-cookbook/. Free | Biquad coefficient formulas for every standard filter. L1.9, L5.6 |
| `[EARLEVEL]` ⭐ | **Nigel Redmon: EarLevel Engineering blog**, earlevel.com. Free | Short, practical articles on ADSR envelopes, wavetable oscillators, biquads, one-pole filters, and sample rate conversion. L1.3, L1.9, L4.3 |
| `[VALIMAKI07]` | **Välimäki & Huovilainen: "Antialiasing Oscillators in Subtractive Synthesis"** (IEEE Signal Processing Magazine, 2007) | The review that introduced **PolyBLEP**. L1.8 |
| `[STILSON-BLIT]` | **Stilson & Smith: "Alias-Free Digital Synthesis of Classic Analog Waveforms"** (ICMC 1996). Free on CCRMA | The BLIT approach, and the historical root of band-limited oscillators. L1.8 |
| `[BLEP-SNDKIT]` | **Paul Batchelor: sndkit "blep"**, paulbatchelor.github.io/sndkit/blep. Free | A tiny, readable PolyBLEP implementation write-up. L1.8 |
| `[PARKER-ADAA]` | **Parker, Zavalishin & Le Bivic: "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution"** (DAFx-16). Free | Antiderivative antialiasing (ADAA). L4.5, L5.2 |
| `[WAKEFIELD-GEN]` | **Wakefield & Taylor: *Generating Sound & Organizing Time: Thinking with gen~*, Book 1** (Cycling '74, 2022) 💰 | Teaches *per-sample thinking* beautifully. The patches are Max/gen~, but the ideas map directly to C++ |
| `[AUDIO-PROG-BOOK]` | **Boulanger & Lazzarini (eds.): *The Audio Programming Book*** (MIT Press, 2010) 💰 | C-based, from WAV files up to oscillators and filters. Good for Branch 1's "from scratch" spirit |
| `[MUSICDSP]` | **musicdsp.org archive**. Free | An old forum-snippet archive. Quality varies wildly, which makes it great for "why is this wrong?" exercises |

## D. Effects

| ID | Resource | Why / when |
|----|----------|-----------|
| `[SIGNALSMITH-REVERB]` ⭐ | **Geraint Luff: "Let's Write A Reverb"** (Signalsmith Audio, 2021, plus ADC'21 talk). Free | The best modern FDN reverb walkthrough. L5.5 |
| `[DATTORRO]` ⭐ | **Jon Dattorro: "Effect Design Part 1: Reverberator and Other Filters"** (JAES 1997), ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf. Part 2 covers delay modulation and interpolation. Free | The plate reverb everyone implements, plus a lot of practical wisdom. L5.3–L5.5 |
| `[VALHALLA]` | **Sean Costello: Valhalla DSP blog**, especially "Getting Started With Reverb Design" (2021). Free | A reverb master's reading list and philosophy. L5.5 |
| `[REISS-FX]` | **Reiss & McPherson: *Audio Effects: Theory, Implementation and Application*** (CRC, 2014) 💰 | A clear textbook on delay, modulation, filters, dynamics, and distortion |
| `[ZOLZER-DAFX]` | **Udo Zölzer (ed.): *DAFX: Digital Audio Effects*, 2nd ed.** 💰 | The academic reference |
| `[GIANNOULIS-COMP]` | **Giannoulis, Massberg & Reiss: "Digital Dynamic Range Compressor Design: A Tutorial and Analysis"** (JAES 2012). Free preprints exist | How to build a compressor properly. L5.6 |

## E. Real-time, C++, and plugins

| ID | Resource | Why / when |
|----|----------|-----------|
| `[BENCINA]` ⭐ | **Ross Bencina: "Real-time audio programming 101: time waits for nothing"** (2011), rossbencina.com. Free | *The* essay on why you can't allocate, lock, or do I/O on the audio thread. L2.2 |
| `[RT101]` | **Dave Rowland & Fabian Renn-Giles: "Real-time 101", parts I & II** (ADC 2019, YouTube). Free | Practical patterns for real-time-safe C++ |
| `[DOUMLER]` | **Timur Doumler's talks**: "C++ in the Audio Industry", "Using Locks in Real-Time Audio Processing, Safely", and others (CppCon/ADC, YouTube). Free | Modern C++ applied to audio by a C++ committee member |
| `[FARBOT]` | **farbot** (github.com/hogliux/farbot). Free | Small library of real-time-safe sharing patterns. Read it *after* writing your own |
| `[MOODYCAMEL]` | **moodycamel ReaderWriterQueue** (github.com/cameron314/readerwriterqueue). Free | A production SPSC queue to compare against yours. L2.3 |
| `[PRESHING]` | **Jeff Preshing's blog** on lock-free programming and memory ordering (preshing.com). Free | Acquire/release semantics, explained with diagrams. L2.3 |
| `[MINIAUDIO]` | **miniaudio** (github.com/mackron/miniaudio, public domain / MIT-0). Free | Single-header audio I/O. L2.1 |
| `[MIDI-SPEC]` | **MIDI 1.0 specification** (midi.org, free account). Summary tables are all over the web | Message formats. L2.3 |
| `[CLAP]` ⭐ | **CLAP SDK** (github.com/free-audio/clap, MIT). Read `include/clap/*.h`; the comments *are* the documentation | Simple, C-based, and thread-annotated. L3.2 |
| `[CLAP-WRAPPER]` | **clap-wrapper** (github.com/free-audio/clap-wrapper, MIT) | Turns your CLAP into a VST3 / AUv2 / standalone. L3.3 |
| `[CLAP-VALIDATOR]` | **clap-validator** (github.com/free-audio/clap-validator) | Automated conformance tests for your CLAP. L3.2 |
| `[VST3]` | **VST 3 SDK** (github.com/steinbergmedia/vst3sdk, **MIT since v3.8, Oct 2025**) and the VST3 dev portal | L3.3 if you go raw VST3 |
| `[JUCE]` | **JUCE 8** (juce.com, tutorials + docs; AGPLv3 or JUCE licence with a free Starter tier) | L3.3, L3.5 if chosen |
| `[PLUGINVAL]` | **pluginval** (Tracktion, github.com/Tracktion/pluginval) | Stress-tests VST3/AU plugins. L3.3, L6.4 |
| `[AUDIO-PROGRAMMER]` | **The Audio Programmer** community (Discord, YouTube, meetups). Free | People to ask. Also the source of many good JUCE and DSP videos |
| `[KVR-DSP]` | **KVR Audio "DSP and Plugin Development" forum**. Free | Where the experts (including Zavalishin and Simper) have answered questions for 20 years. Search before asking |

## F. Open-source synths to read (not copy, unless the license allows)

| ID | Project | License | Read it for |
|----|---------|---------|------------|
| `[SURGE]` | Surge XT (github.com/surge-synthesizer/surge) | GPLv3 | A huge, mature hybrid synth: oscillators, filters, mod matrix, FX, voice management |
| `[VITAL]` | Vital (github.com/mtytel/vital) | GPLv3 | Wavetables, SIMD voice processing, modern modulation UX |
| `[MI]` | Mutable Instruments Eurorack (github.com/pichenettes/eurorack) | MIT | Beautifully small embedded DSP. *Plaits* has compact FM and other models |
| `[MSFA]`/`[DEXED]` | (above) | Apache 2.0 / GPLv3 | DX7-faithful FM |
| `[SIGNALSMITH-DSP]` | Signalsmith DSP library (github.com/Signalsmith-Audio/dsp) | MIT | Clean modern C++ delay lines, FFT, envelopes |

## G. Tools

| Tool | Use |
|------|-----|
| **Python + numpy + scipy + matplotlib** | Your analysis bench: spectra, spectrograms, filter responses (tools/analyze/) |
| **Audacity** (free) | Quick listen, waveform zoom, "Plot Spectrum" |
| **Sonic Visualiser** (free) | Excellent spectrograms with log frequency axes, for seeing aliasing |
| **Plugin Doctor** (DDMF) 💰, trial available | Measures a plugin's frequency response, harmonics, and aliasing in real time |
| **REAPER** (generous trial, cheap license) | A light, CLAP-capable test host, even if your main DAW is different |
| **loopMIDI** + **VMPK** (free) | Virtual MIDI port + on-screen keyboard, if no hardware keyboard is handy |
| **Vital, Surge XT, Dexed** (free) | Instruments to learn on and compare against |
