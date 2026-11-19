// fss/synth/presets.h: a few factory patches shared by every host (offline renders, standalone, later
// the plugin). Introduced in L2.1 when a second host needed the same sounds; values are the ones designed
// and measured in L1.5–L2.5 (see those journals), plus four from L3.4 (bell, brass, organ, marimba).
#pragma once

#include <fss/synth/patch.h>
#include <fss/util/math.h>

namespace fss::synth::presets {

/// Two-stack e-piano (algorithm 5): tine A(×14, fast decay) > B, body C > D. Velocity = brightness.
inline Patch epiano()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{14.0}, .level = 1.6f, .velSens = 0.9f, .keyLevelDbPerOct = 3.0f, .keyRateScale = 0.5f,
                .env = {.attackMs = 1.0f, .decayMs = 180.0f, .sustain = 0.0f, .releaseMs = 100.0f}};
    p.ops[1] = {.level = 0.6f, .velSens = 0.5f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 1200.0f, .sustain = 0.0f, .releaseMs = 300.0f}};
    p.ops[2] = {.level = 1.1f, .velSens = 0.7f, .keyLevelDbPerOct = 3.0f, .keyRateScale = 0.3f,
                .env = {.attackMs = 1.0f, .decayMs = 1500.0f, .sustain = 0.25f, .releaseMs = 300.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.4f, .keyRateScale = 0.3f,
                .env = {.attackMs = 2.0f, .decayMs = 3000.0f, .sustain = 0.3f, .releaseMs = 400.0f}};
    p.outputDb = util::gainToDb(0.22f);
    return p;
}

/// FM bass: only C > D of the stack, C with self-feedback and a fast-decaying index.
inline Patch fmBass()
{
    Patch p;
    p.algorithm = kAlgorithms[0];
    p.algorithm.mod[2][2] = 0.9f;
    p.ops[0].level = p.ops[1].level = 0.0f;
    p.ops[2] = {.level = 2.2f, .velSens = 0.6f, .env = {.attackMs = 1.0f, .decayMs = 350.0f, .sustain = 0.25f, .releaseMs = 80.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.3f, .env = {.attackMs = 2.0f, .decayMs = 800.0f, .sustain = 0.7f, .releaseMs = 60.0f}};
    p.outputDb = util::gainToDb(0.45f);
    return p;
}

/// Resonant FM pluck (L1.10): a feedback-saw-like C>D pair through a resonant low-pass whose envelope
/// opens it 5 octaves and closes it again in ~0.3 s. Key tracking 0.5 keeps high notes from going dull;
/// velocity opens the filter (soft notes are 2 octaves darker).
inline Patch resoPluck()
{
    Patch p;
    p.algorithm = kAlgorithms[0]; // stack: only C>D sound (A and B silent)
    p.algorithm.mod[2][2] = 1.0f; // C feedback: C's sine becomes saw-like, so D's spectrum is rich
    p.ops[0].level = p.ops[1].level = 0.0f;
    p.ops[2] = {.level = 2.0f, .env = {.attackMs = 1.0f, .decayMs = 2000.0f, .sustain = 0.8f, .releaseMs = 200.0f}};
    p.ops[3] = {.level = 1.0f, .env = {.attackMs = 1.0f, .decayMs = 1500.0f, .sustain = 0.5f, .releaseMs = 250.0f}};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 250.0f, .q = 5.0f, .envOctaves = 5.0f, .keyTrack = 0.5f,
                .velOctaves = 2.0f, .env = {.attackMs = 1.0f, .decayMs = 300.0f, .sustain = 0.0f, .releaseMs = 300.0f}};
    p.outputDb = util::gainToDb(0.22f);
    return p;
}

/// Wobble bass (L1.11): the FM bass through a resonant low-pass that the global LFO 2 sweeps ±2.5 octaves
/// at 3 Hz (triangle, restarted on each note so every note wobbles the same way).
inline Patch wobbleBass()
{
    Patch p = fmBass();
    p.ops[2].level = 3.0f; // brighter source: the filter needs harmonics to sweep through
    p.ops[2].env.sustain = 0.6f;
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 400.0f, .q = 6.0f, .keyTrack = 0.5f,
                .env = {.attackMs = 1.0f, .decayMs = 300.0f, .sustain = 0.0f, .releaseMs = 200.0f}};
    p.lfo2 = {.rateHz = 3.0f, .shape = dsp::LfoShape::Triangle, .keySync = true};
    p.routes.lfo2CutoffOctaves = 2.5f;
    p.outputDb = util::gainToDb(0.3f);
    return p;
}

/// Evolving FM pad (L1.11): two slow stacks (algorithm 5) whose brightness drifts with a slow smooth-random
/// LFO 2 on the FM index, plus a gentle free-running vibrato that fades in.
inline Patch evolvingPad()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{2.0}, .detune = util::Cents{4.0}, .level = 1.4f,
                .env = {.attackMs = 900.0f, .decayMs = 3000.0f, .sustain = 0.7f, .releaseMs = 1500.0f}};
    p.ops[1] = {.level = 0.5f, .env = {.attackMs = 600.0f, .decayMs = 2000.0f, .sustain = 0.9f, .releaseMs = 1800.0f}};
    p.ops[2] = {.ratio = Ratio{3.0}, .detune = util::Cents{-5.0}, .level = 1.0f,
                .env = {.attackMs = 1500.0f, .decayMs = 4000.0f, .sustain = 0.6f, .releaseMs = 1500.0f}};
    p.ops[3] = {.ratio = Ratio{1.0}, .detune = util::Cents{3.0}, .level = 0.5f,
                .env = {.attackMs = 800.0f, .decayMs = 3000.0f, .sustain = 0.9f, .releaseMs = 2000.0f}};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 3500.0f, .q = 0.9f, .keyTrack = 0.3f};
    p.lfo1 = {.rateHz = 4.8f, .shape = dsp::LfoShape::Sine, .delayMs = 400.0f, .fadeMs = 1200.0f, .keySync = false};
    p.lfo2 = {.rateHz = 0.2f, .shape = dsp::LfoShape::SmoothRandom, .keySync = false};
    p.routes = {.vibratoSemitones = 0.12f, .wheelVibratoSemitones = 0.5f, .lfo2CutoffOctaves = 0.7f, .lfo2IndexAmount = 0.8f};
    p.outputDb = util::gainToDb(0.25f);
    return p;
}

/// Legato bass (L2.5): the Reso pluck's sound as a mono legato synth with an 80 ms glide between
/// overlapping notes (lowest-note priority would suit a pedal-tone bass line; this one uses last-note).
inline Patch legatoBass()
{
    Patch p = resoPluck();
    p.ops[2].env.sustain = 0.9f;
    p.ops[3].env = {.attackMs = 2.0f, .decayMs = 800.0f, .sustain = 0.8f, .releaseMs = 120.0f};
    p.filter.env = {.attackMs = 1.0f, .decayMs = 400.0f, .sustain = 0.25f, .releaseMs = 200.0f};
    p.filter.envOctaves = 4.0f;
    p.voice = {.mode = VoiceMode::Legato, .glideMs = 80.0f, .glideLegatoOnly = true};
    p.outputDb = util::gainToDb(0.3f);
    return p;
}

/// Tubular bell (L3.4): Chowning's inharmonic recipe. Stack 1: A(×3.5) > B, a long-decaying index, so
/// the clang mellows into a near-sine as it rings. Stack 2: C(×7, 60 ms) > D(×3) adds the strike.
inline Patch bell()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{3.5}, .level = 2.5f, .velSens = 0.6f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 4000.0f, .sustain = 0.0f, .releaseMs = 2500.0f}};
    p.ops[1] = {.level = 1.0f, .velSens = 0.3f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 7000.0f, .sustain = 0.0f, .releaseMs = 3000.0f}};
    p.ops[2] = {.ratio = Ratio{7.0}, .level = 1.8f, .velSens = 0.8f,
                .env = {.attackMs = 1.0f, .decayMs = 60.0f, .sustain = 0.0f, .releaseMs = 60.0f}};
    p.ops[3] = {.ratio = Ratio{3.0}, .level = 0.35f, .velSens = 0.5f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 2500.0f, .sustain = 0.0f, .releaseMs = 1500.0f}};
    p.outputDb = util::gainToDb(0.3f);
    return p;
}

/// FM brass (L3.4): Chowning's other classic. One 1:1 pair whose INDEX envelope follows the loudness
/// envelope, so the note gets brighter as it swells (the "blat"), with feedback for extra harmonics, a delayed
/// vibrato, and a key-tracked low-pass to keep the top end from fizzing.
inline Patch brass()
{
    Patch p;
    p.algorithm = kAlgorithms[0];
    p.algorithm.mod[2][2] = 0.35f;
    p.ops[0].level = p.ops[1].level = 0.0f;
    p.ops[2] = {.level = 3.2f, .velSens = 0.7f,
                .env = {.attackMs = 70.0f, .decayMs = 400.0f, .sustain = 0.65f, .releaseMs = 140.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.4f,
                .env = {.attackMs = 45.0f, .decayMs = 300.0f, .sustain = 0.85f, .releaseMs = 160.0f}};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 2800.0f, .q = 0.8f, .keyTrack = 0.6f};
    p.lfo1 = {.rateHz = 5.5f, .shape = dsp::LfoShape::Sine, .delayMs = 350.0f, .fadeMs = 500.0f, .keySync = true};
    p.routes.vibratoSemitones = 0.1f;
    p.outputDb = util::gainToDb(0.25f); // measured (l3.4): 0.35 peaked at -1.7 dBFS, into the soft clip
    return p;
}

/// Drawbar organ (L3.4): algorithm 8, four carriers at 16', 8', 4' and 2 2/3' (ratios 0.5, 1, 2, 3), organ
/// envelopes (instant on, full sustain, quick off), a touch of feedback on the top drawbar for grit, and a
/// free-running vibrato standing in for a scanner chorus.
inline Patch organ()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[7];
    p.algorithmNumber = 8;
    const dsp::AdsrParams env{.attackMs = 3.0f, .decayMs = 200.0f, .sustain = 1.0f, .releaseMs = 50.0f};
    p.ops[0] = {.ratio = Ratio{0.5}, .level = 0.8f, .env = env};
    p.ops[1] = {.ratio = Ratio{1.0}, .level = 1.0f, .env = env};
    p.ops[2] = {.ratio = Ratio{2.0}, .level = 0.7f, .env = env};
    p.ops[3] = {.ratio = Ratio{3.0}, .level = 0.5f, .env = env};
    p.algorithm.mod[3][3] = 0.25f;
    p.lfo1 = {.rateHz = 6.7f, .shape = dsp::LfoShape::Triangle, .keySync = false};
    p.routes.vibratoSemitones = 0.06f;
    p.outputDb = util::gainToDb(0.28f); // measured (l3.4): 0.4 peaked at -2.3 dBFS
    return p;
}

/// Marimba (L3.4): two short stacks (algorithm 5). A(×4) > B is the wooden bar: the index dies in 70 ms, so
/// the mallet's brightness is only at the start. C(×10) > D(×4) is the knock of the mallet. Higher keys
/// decay faster (key rate scaling), like real bars.
inline Patch marimba()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{4.0}, .level = 1.8f, .velSens = 0.9f, .keyRateScale = 0.5f,
                .env = {.attackMs = 1.0f, .decayMs = 70.0f, .sustain = 0.0f, .releaseMs = 70.0f}};
    p.ops[1] = {.level = 1.0f, .velSens = 0.5f, .keyRateScale = 0.6f,
                .env = {.attackMs = 1.0f, .decayMs = 900.0f, .sustain = 0.0f, .releaseMs = 400.0f}};
    p.ops[2] = {.ratio = Ratio{10.0}, .level = 1.2f, .velSens = 0.9f,
                .env = {.attackMs = 1.0f, .decayMs = 25.0f, .sustain = 0.0f, .releaseMs = 25.0f}};
    p.ops[3] = {.ratio = Ratio{4.0}, .level = 0.3f, .velSens = 0.7f, .keyRateScale = 0.6f,
                .env = {.attackMs = 1.0f, .decayMs = 220.0f, .sustain = 0.0f, .releaseMs = 150.0f}};
    p.outputDb = util::gainToDb(0.6f); // measured (l3.4): 0.4 peaked at only -9.3 dBFS
    return p;
}

/// E-piano + chorus (L5.4): the classic pairing. The E-piano patch untouched (so `l1.7_piece.wav`, which uses
/// it, keeps its hash), with a slow two-voice chorus at 50 % and the width opened a little, because the
/// chorus is what makes the mono voice stereo. A new preset rather than a change to "E-piano": players can
/// A/B the two.
inline Patch epianoChorus()
{
    Patch p = epiano();
    p.fx.chorus = {.enabled = true, .rateHz = 0.5f, .depthMs = 3.5f, .delayMs = 8.0f, .voices = 2, .mixPct = 50.0f};
    p.fx.width = {.enabled = true, .widthPct = 130.0f};
    return p;
}

/// Evolving matrix (L4.1): the FM pad's operators with five matrix routes, so every note moves on its own:
///   1. Mod env (3 s rise, slow fall)  → FM index  +60 %   the tone opens over seconds, then settles
///   2. LFO 1 (free, 0.3 Hz)           → Op A pitch +1 %   op A drifts ±0.24 st: a slow beating shimmer
///   3. Random (per note)              → Cutoff     +20 %  each note a different brightness (±1.6 oct)
///   4. LFO 2 (0.13 Hz triangle)       → Amp        +8 %   a slow ±1.9 dB swell across the whole chord
///   5. Key × Mod env                  → Op C level −40 %  high notes lose op C's bite as the envelope rises
/// The L1.11 routes are off (their matrix slots are free for anything).
inline Patch evolvingMatrix()
{
    Patch p = evolvingPad();
    p.routes = {.vibratoSemitones = 0.0f, .wheelVibratoSemitones = 0.5f, .lfo2CutoffOctaves = 0.0f, .lfo2IndexAmount = 0.0f};
    p.lfo1 = {.rateHz = 0.3f, .shape = dsp::LfoShape::Sine, .keySync = false};
    p.lfo2 = {.rateHz = 0.13f, .shape = dsp::LfoShape::Triangle, .keySync = false};
    p.mod.env = {.attackMs = 3000.0f, .decayMs = 6000.0f, .sustain = 0.4f, .releaseMs = 2500.0f};
    p.mod.slots[0] = {.src = ModSrc::ModEnv, .dst = ModDst::Index, .amountPct = 60.0f};
    p.mod.slots[1] = {.src = ModSrc::Lfo1, .dst = ModDst::OpAPitch, .amountPct = 1.0f};
    p.mod.slots[2] = {.src = ModSrc::Random, .dst = ModDst::Cutoff, .amountPct = 20.0f};
    p.mod.slots[3] = {.src = ModSrc::Lfo2, .dst = ModDst::Amp, .amountPct = 8.0f};
    p.mod.slots[4] = {.src = ModSrc::Key, .via = ModSrc::ModEnv, .dst = ModDst::OpCLevel, .amountPct = -40.0f};
    return p;
}

/// Wide pad (L4.4): the FM pad with 4 unison copies, 18 cents of spread across the full stereo width. The
/// mono-compatibility check in the L4.4 journal is done on this patch.
inline Patch widePad()
{
    Patch p = evolvingPad();
    p.unison = {.voices = 4, .detuneCents = 18.0f, .widthPct = 100.0f};
    return p;
}

/// Bell hall (L5.5): the Bell in the FDN reverb: a 3 s hall with a 20 ms gap before it, the highs dying a
/// little faster than the lows, and the bass kept out of the wash. The patch the L5.5 FL Studio check uses.
inline Patch bellHall()
{
    Patch p = bell();
    p.fx.reverb = {.enabled = true, .decayS = 3.0f, .sizePct = 120.0f, .predelayMs = 20.0f, .dampingPct = 45.0f,
                   .modPct = 30.0f, .lowCutHz = 150.0f, .mixPct = 32.0f};
    return p;
}

/// Saw lead (L4.2): FSS as a subtractive synth. Two saw CARRIERS (algorithm 5 with both modulators silent), 12
/// cents apart, into a resonant low-pass whose envelope opens on every note; legato with a short glide and a
/// delayed vibrato. The classic monophonic VA lead, with no FM at all.
inline Patch sawLead()
{
    using util::Cents;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    const dsp::AdsrParams amp{.attackMs = 3.0f, .decayMs = 600.0f, .sustain = 0.85f, .releaseMs = 180.0f};
    p.ops[0].level = p.ops[2].level = 0.0f;
    p.ops[1] = {.detune = Cents{-6.0}, .level = 1.0f, .velSens = 0.3f, .env = amp, .wave = 6};
    p.ops[3] = {.detune = Cents{6.0}, .level = 1.0f, .velSens = 0.3f, .env = amp, .wave = 6};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 900.0f, .q = 2.5f, .envOctaves = 3.5f, .keyTrack = 0.5f,
                .velOctaves = 1.0f, .env = {.attackMs = 2.0f, .decayMs = 350.0f, .sustain = 0.25f, .releaseMs = 200.0f}};
    p.voice = {.mode = VoiceMode::Legato, .glideMs = 70.0f};
    p.lfo1 = {.rateHz = 5.5f, .shape = dsp::LfoShape::Sine, .delayMs = 300.0f, .fadeMs = 400.0f, .keySync = true};
    p.routes.vibratoSemitones = 0.12f;
    p.fx.delay = {.enabled = true, .feedbackPct = 30.0f, .mixPct = 18.0f};
    p.outputDb = util::gainToDb(0.5f);
    return p;
}

/// Supersaw (L4.2, the patch L4.4's unison was waiting for): ONE saw carrier (algorithm 1, the three modulators
/// silent) times 7 unison copies spread over 40 cents and the full stereo width: seven detuned saws per note, the
/// trance/"JP" pad and lead. A gentle key-tracked low-pass takes the edge off, and a little hall glues it.
inline Patch supersaw()
{
    Patch p;
    p.algorithm = kAlgorithms[0];
    p.ops[0].level = p.ops[1].level = p.ops[2].level = 0.0f;
    p.ops[3] = {.level = 1.0f, .velSens = 0.2f,
                .env = {.attackMs = 8.0f, .decayMs = 300.0f, .sustain = 1.0f, .releaseMs = 450.0f}, .wave = 6};
    p.unison = {.voices = 7, .detuneCents = 40.0f, .widthPct = 100.0f};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 6000.0f, .q = 0.7071f, .keyTrack = 0.3f};
    p.fx.reverb = {.enabled = true, .decayS = 2.5f, .predelayMs = 15.0f, .mixPct = 20.0f};
    p.outputDb = util::gainToDb(0.18f); // measured (l4.2): 0.35 drove a 4-note chord into the soft clip
    return p;
}

/// Wave bass (L4.2, TX81Z-style): the sine variants doing what they did on Yamaha's 4-op synths. Algorithm 5:
/// an Abs sine modulator (an octave-up spectrum, with feedback) plucks a Half sine carrier (soft, even
/// harmonics); a Camel at ratio 2 adds a short, bright attack to a sine sub-carrier.
inline Patch waveBass()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.level = 1.6f, .velSens = 0.6f, .env = {.attackMs = 1.0f, .decayMs = 180.0f, .sustain = 0.15f, .releaseMs = 80.0f},
                .wave = 2};
    p.ops[1] = {.level = 1.0f, .velSens = 0.3f, .env = {.attackMs = 1.0f, .decayMs = 900.0f, .sustain = 0.6f, .releaseMs = 120.0f},
                .wave = 1};
    p.ops[2] = {.ratio = Ratio{2.0}, .level = 0.9f, .velSens = 0.7f,
                .env = {.attackMs = 1.0f, .decayMs = 90.0f, .sustain = 0.0f, .releaseMs = 60.0f}, .wave = 5};
    p.ops[3] = {.level = 0.5f, .env = {.attackMs = 1.0f, .decayMs = 400.0f, .sustain = 0.4f, .releaseMs = 100.0f}};
    p.algorithm.mod[0][0] = 0.4f;
    p.outputDb = util::gainToDb(0.9f); // measured (l3.4): 0.5 peaked at only -9.8 dBFS
    return p;
}

/// Wave clav (L4.2, TX81Z-style): a percussive keyboard from the "nasal" waves. An Alt sine carrier struck by a
/// Quarter sine modulator at ratio 3 (bright, its index dies in ~120 ms), plus a quiet Square carrier tickled by a
/// short ratio-7 sine for the pick. Velocity opens the brightness.
inline Patch waveClav()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{3.0}, .level = 1.4f, .velSens = 0.8f,
                .env = {.attackMs = 1.0f, .decayMs = 120.0f, .sustain = 0.2f, .releaseMs = 60.0f}, .wave = 3};
    p.ops[1] = {.level = 1.0f, .velSens = 0.4f, .env = {.attackMs = 1.0f, .decayMs = 600.0f, .sustain = 0.3f, .releaseMs = 60.0f},
                .wave = 4};
    p.ops[2] = {.ratio = Ratio{7.0}, .level = 0.6f, .velSens = 0.8f,
                .env = {.attackMs = 1.0f, .decayMs = 30.0f, .sustain = 0.0f, .releaseMs = 30.0f}};
    p.ops[3] = {.level = 0.35f, .velSens = 0.5f, .env = {.attackMs = 1.0f, .decayMs = 300.0f, .sustain = 0.2f, .releaseMs = 60.0f},
                .wave = 7};
    p.outputDb = util::gainToDb(0.5f);
    return p;
}

/// The factory list hosts show (fss_play, the plugin editor). Since L3.4 the factory bank also exists as
/// text files in presets/factory/ (written from these functions by `fss_render l3.4`; a test checks that
/// the files and these functions agree), so the files are what users read, copy and edit.
struct NamedPreset {
    const char* name;
    Patch (*make)();
};
inline constexpr NamedPreset kFactoryPresets[] = {
    {"E-piano", epiano},
    {"FM bass", fmBass},
    {"Reso pluck", resoPluck},
    {"Wobble bass", wobbleBass},
    {"FM pad", evolvingPad},
    {"Legato bass", legatoBass},
    {"Bell", bell},
    {"Brass", brass},
    {"Organ", organ},
    {"Marimba", marimba},
    {"E-piano chorus", epianoChorus}, // L5.4. Append only: the list position is the file number ("11 ...")
    {"Evolving matrix", evolvingMatrix}, // L4.1
    {"Wide pad", widePad},               // L4.4
    {"Bell hall", bellHall},             // L5.5
    {"Saw lead", sawLead},               // L4.2
    {"Supersaw", supersaw},              // L4.2
    {"Wave bass", waveBass},             // L4.2
    {"Wave clav", waveClav},             // L4.2
    {"Init", [] { return Patch{}; }},
};

} // namespace fss::synth::presets
