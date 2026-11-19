# CFS in FL Studio: install, test, report back (milestone M5)

Claude can't run FL Studio, so everything below is **for the owner to try**. Each step says what should happen;
anything different is worth a note in the journal (or a message to the next session). Menu names are from FL
Studio 2024/2025 and may differ slightly in other versions.

What has been checked without FL (see the L3.2–L3.5 journals): clap-validator 0.4.1 passes all 34 applicable
tests; `fss_clap_host` (our headless host) loads the DLL, plays bit-exactly the same audio as the bare engine,
saves/loads state, re-activates at 22.05–192 kHz, opens two editors at once, and drags the cutoff knob with real
Win32 mouse messages, after which the host receives GESTURE_BEGIN, PARAM_VALUE…, GESTURE_END.

## 1. Build and install
```powershell
. .\tools\devshell.ps1
cmake --workflow --preset release          # builds build\release\hosts\clap\CFS.clap and runs every test
```
Then, in a PowerShell started with **Run as administrator** (writing to Program Files needs it):
```powershell
.\tools\install_clap.ps1                   # copies CFS.clap to C:\Program Files\Common Files\CLAP
```
No admin rights? `.\tools\install_clap.ps1 -Dest D:\Plugins\CLAP` (any folder), then add that folder in FL (step 2).
**Close FL Studio before re-installing**: FL keeps the DLL open while it's loaded, and the copy fails ("file in use").

## 2. Let FL Studio find it
1. **Options > Manage plugins** (the Plugin Manager).
2. Under **Plugin search paths**, check that `C:\Program Files\Common Files\CLAP` is listed (it is by default);
   add your own folder here if you installed elsewhere.
3. Tick **Verify plugins** and press **Find installed plugins** (or **Start scan**).
   - Expect: **CFS** by **ClaudeFS** in the list, type CLAP, category *Generator* (we declare `instrument`,
     `synthesizer`, `stereo`). Its row should not be red (red = failed to load).
4. Tick the **star / Fav** box next to CFS so it shows up in FL's "Add" menus.
5. After installing a *new build*, scan again (FL caches the plugin's name, parameters and ports).

## 3. Play it
1. **Channel rack > + (Add) > CFS**, or drag it from **Browser > Plugin database > Generators**.
   - Expect: FL's wrapper window with the **CFS editor** inside: algorithm diagram on the left, operators A–D in
     the middle, filter and LFOs on the right, presets and meters on top. Size about 1120×700 at 100 % scaling.
2. Play your MIDI keyboard (FL routes it to the selected channel) or the computer keyboard / piano roll.
   - Expect: sound, the **voices** counter and the **peak meter** moving.
3. Pick presets from the menu at the top (E-piano, FM bass, Bell, Brass, Organ, Marimba...).
   - Expect: knobs jump to the preset's values; the sound changes on the next notes (sounding notes glide to
     some new values over ~10 ms).
4. Turn knobs: drag up/down, **shift**-drag for fine steps, **double-click** for the default. Click an algorithm
   thumbnail to switch the algorithm: the big diagram redraws, operators show "carrier" / "modulates …".
5. Pitch bend, mod wheel (vibrato depth) and sustain pedal: these reach CFS as MIDI only if FL forwards them to
   CLAP plugins; please report whether they work.

## 4. Automate (the M5 check)
Record a knob gesture:
1. Turn on FL's record button with **Automation** included in what it records, press play, and turn CFS's
   **Cutoff** knob in CFS's own editor for a few bars. Stop.
   - Expect: FL created automation for "Cutoff" (an automation clip, or events in the pattern, depending on your
     recording settings), shaped like your movement.
   Alternatively: turn the knob once, then **Tools > Last tweaked > Create automation clip**.
2. Play it back without touching anything.
   - Expect: the **Cutoff knob in CFS's editor moves by itself** following the clip, and the sound follows.
3. Draw an automation clip by hand from 0 % to 100 %.
   - Expect: the knob sweeps 20 Hz → 20 kHz with the clip's *middle* at about **630 Hz** (CFS exposes continuous
     parameters as knob positions, so the sweep is even in pitch, not in Hz: see DECISIONS.md, L3.2 entry).
4. FL's own parameter list: in the wrapper menu, **Browse parameters**. Expect 151 parameters as of L4.4 (67 global + 32 mod matrix + 13 per operator; Phase F adds more, see section 7), grouped with
   names like "Cutoff", "Op A Ratio"; menus (filter mode, LFO shape) show names, not numbers.

## 5. Save with the project
1. Change a few knobs, pick a preset, tweak it, then **File > Save as** `cfs_test.flp`.
2. **File > New**, then reopen `cfs_test.flp`.
   - Expect: CFS comes back with exactly your knob positions and the preset name in the top bar; the automation
     clip still plays the knob.
3. Optional: save a CFS preset (type a name next to **Save**). It lands in `Documents\CFS Presets\` as a text file
   you can open in Notepad; **Rescan** lists it under "yours".

## 6. What to report (and the trace that answers most of it)
Set a user environment variable and restart FL Studio:
```powershell
[Environment]::SetEnvironmentVariable("CFS_CLAP_TRACE", "1", "User")      # turn on
[Environment]::SetEnvironmentVariable("CFS_CLAP_TRACE", $null, "User")    # turn off again later
```
CFS then appends every host call to `%TEMP%\cfs_clap_trace.txt`, plus an audio-thread summary each time FL
deactivates it. Useful questions it answers: Does FL call `state.load` before or after `activate`? What block
sizes does it use (`frames min..max`)? Does it send notes as CLAP notes or as MIDI (`note on` vs `midi` counts)?
Does it ever send 0-frame blocks? Does it call `gui.set_scale` (and with what value)?

Things only you can tell:
- Does it sound right? (The ten factory presets were levelled by measurement, never heard.)
- Does **typing** in the preset-name box work inside FL's wrapper? (Hosts often keep the keyboard.)
- Does **resizing** the plugin window work, and does it look sharp on your display scaling?
- CPU: FL's CPU meter with 16 notes held should be in the same range as `fss_play` (about 5–15 % of a core).

## 7. Phase F features (L5.1 onward): what to try, and what you should hear
Each lesson adds a subsection. Reinstall the plugin after each build (`tools\install_clap.ps1`), then rescan.

### L5.1: the FX page, width, and the tail
1. Open CFS's editor: the top bar now has **Synth** and **FX** buttons. Click **FX**: one panel, **Width**.
   - Expect: everything you did in M5 still works on the Synth page; the FX page shows the Width switch and knob.
2. Play a chord, switch **Width on** and turn **Width** from 0 % to 200 % while it sounds.
   - Expect: **no change at all**. CFS's voices are mono until L4.4 (unison), and width can only scale a
     difference between the channels. This step checks that switching an effect while playing doesn't click.
3. FL's parameter list (**Browse parameters**) now ends with "Width on" and "Width" in a "Width" module.
4. Optional, with `CFS_CLAP_TRACE=1`: nothing new to look for yet; the tail extension reports 0 until the delay.

### L5.3: the synced delay at the project tempo
1. Set FL's tempo to **120**. Load the **E-piano** preset. On the FX page, switch the Delay **On** (defaults:
   Sync on, Note **1/8 D**, Feedback 35 %, Ping-pong on, Mix 25 %).
2. Play short staccato notes, with headphones.
   - Expect: echoes every **375 ms**, the first on the **left**, the next on the right, and so on, each a
     little quieter and a little darker.
3. Change FL's tempo to **90** while holding a note or letting echoes ring.
   - Expect: the echoes move to **500 ms** within a beat, with no click (Glide = Clean: a 50 ms crossfade).
4. Turn **Sync** off and sweep **Time** with the mouse while echoes ring, first with Glide = **Clean** (stepwise,
   no pitch change), then **Tape** (the echoes bend up as you shorten the time, down as you lengthen it).
   - Report: does Clean ever tick? Does Tape sound like a tape echo or like a glitch?
5. Switch the delay **Off** while echoes ring.
   - Expect: the echoes already sounding **ring out**; new notes have no echo.
6. Draw a **tempo ramp** in FL's playlist (tempo automation) from 120 to 140 over two bars with the delay synced.
   - Report: do the echoes follow smoothly, or in audible steps?
7. With `CFS_CLAP_TRACE=1`, stop playback and look at the last "audio thread summary" line in
   `%TEMP%\cfs_clap_trace.txt`: it now ends with `tempo <first> -> <last> BPM (<n> changes)`. If it says
   `tempo 0.000`, FL isn't sending a transport to CFS and sync falls back to 120 BPM. Please report that line.
8. Render the pattern to WAV with the delay on and high feedback (FL's export): the file should include the
   echoes after the last note (CFS reports its tail through `clap.tail`). Report whether the tail gets cut.

### L5.4: chorus on the e-piano, flanger, phaser
1. Load **E-piano**, play a few chords, then load **E-piano chorus** (the new factory preset) and play the same.
   - Expect: the chorused one is **wider** (the sound spreads left and right on headphones) and gently
     swirling, at about the same loudness (it measures 2 dB quieter in mono, about equal in stereo).
   - Report: does it sound like the classic chorused FM e-piano? Too much, too little? (Try Chorus **Rate**
     0.3–1 Hz and **Depth** 2–5 ms; **Voices** 1 is the leanest, 4 the lushest.)
2. Press FL's **mono** button on the master (or pan CFS's mixer track to mono) with E-piano chorus.
   - Expect: it gets narrower and a little quieter, but doesn't hollow out or go "phasey".
3. On any bright patch (Brass, Organ), switch **Flanger on**: a slow jet sweep. Raise **Feedback** to 80 %:
   it starts to ring at a pitch that moves. Set it to **−80 %**: hollower, more nasal.
4. Switch the flanger off and the **Phaser on**: a smoother swirl with no pitch to it. Change **Stages** from 4 to
   12 while a note holds.
   - Expect: more stages = more notches = a deeper, more complex sweep, and the change makes no click.
5. Change **Voices** on the chorus while holding a chord: no clicks, the swirl just thickens or thins.

### L4.1: the mod matrix
1. Load **FM pad**, **Brass** and **Organ** and play them as you did before this version: they should sound
   **exactly** as before (the old LFO routes are now the matrix's fixed slots; tested bit for bit).
2. Load **Evolving matrix** (factory preset 12) and hold a chord for 5 s.
   - Expect: the tone opens over about 3 s (mod env → FM index), op A drifts slowly (a soft shimmer), the
     whole chord swells gently (LFO 2 → amp), and each note you play has a slightly different brightness
     (random → cutoff). Report: evolving, or just wobbly?
3. Make your own route on the **Mod** page: Init preset; slot 1 = Source **LFO 2**, Via **Mod wheel**,
   Dest **Cutoff**, Amount **40 %** (the column on the right shows "+3.20 oct"); switch the Filter (Synth page)
   to Low-pass at about 1 kHz.
   - Expect: with the wheel down, nothing moves; as you raise the wheel, LFO 2 sweeps the filter more and more.
4. Slot 2 = **Velocity → Op D level, −50 %**: harder notes get quieter (a weird but clear test that per-note
   sources work: play soft and hard notes in one chord).
5. Automate a slot's **Amount** with an FL automation clip (Mod wheel or LFO → Pitch is the most sensitive).
   - Known limitation: slot amounts are **not smoothed** yet, so a fast clip on a pitch route may zipper slightly.
     Report whether you can hear it. (Changing a slot's *destination* while a note sounds jumps: it's a menu.)
6. Poly-mod (only if FL supports CLAP per-note modulation, which I couldn't check): in FL's per-note tools
   (piano roll note properties / "note modulation"), modulate CFS's **Cutoff** on one note of a chord.
   - Expect: only that note's filter moves. With `CFS_CLAP_TRACE=1`, the summary line's `param mod` count tells
     whether FL sent any PARAM_MOD at all; please report it.

### L4.4: unison width
1. Load **Wide pad** (factory preset 13) and hold a chord, on headphones.
   - Expect: much wider and thicker than **FM pad** (the same patch at Unison 1), with a slow shimmer from
     the detuned copies.
2. On the Synth page, **Unison** section: while the chord holds, step **Unison** 1 → 4 → 8 → 2 → 1.
   - Expect: it thickens and thins with **no click**; at 1 it's exactly the old mono FM pad.
3. Turn **Unison width** from 100 % to 0 %: the sound narrows to the centre but stays thick (the copies are
   still detuned). Turn **Unison detune** up to 60 cents and beyond: from shimmer to an obvious out-of-tune stack.
4. Mono check: put FL's master (or CFS's mixer track) in **mono** with Wide pad at width 100 %.
   - Expect: a little quieter (I measured −1.5 dB) and narrower, but no hollow or phasey sound.
5. CPU: watch FL's CPU meter with Unison 8 and a big chord (16 notes): about 5× the cost of Unison 1. Report
   if it crackles at your buffer size.
6. Matrix pan: on the Mod page, slot 1 = **LFO 1 → Pan, 80 %** on any patch: each voice swings left and right
   (with LFO 1 free-running they move together; with Key sync on, each note on its own).

### L5.2: drive with oversampling on and off, the crusher, the latency
1. Load **Brass**. FX page, **Drive** panel: **On**, Type **Soft**, Drive **24 dB**, Oversample **1x**. Play a
   few notes high up (C5 to C7).
   - Expect: a saturated, buzzier brass at about the same loudness (Auto gain is on). Switch **Auto gain** off
     for a moment to hear the "louder sounds better" trap: louder and more exciting, but that's only the level.
2. Same, Type **Hard**, Drive 30 dB, and play the highest notes, holding one. Then set Oversample to **4x**.
   - Expect at 1x: a faint, inharmonic whistle or "ring-mod" haze above high notes that doesn't follow the pitch
     (aliasing). At 4x it should be much quieter (measured −32 dB → −56 dB). Report: can you hear 1x vs 4x?
   - The change of Oversample makes CFS ask FL for a restart (CLAP `request_restart`): you may hear a short gap.
     Report what happens: a gap, nothing, or the knob changes but the sound doesn't (then FL ignored the restart;
     the new setting would apply only after FL restarts the plugin, e.g. on the next play or project reload).
3. Latency: with Oversample at **4x**, put a CFS note on the beat next to a kick drum (or a metronome) and render.
   - Expect: no audible offset (CFS reports 37 samples, 0.8 ms; FL should start it that much earlier). FL's
     Mixer shows a plugin's delay in the slot's menu / "Plugin delay compensation" area: report whether CFS
     shows 37 samples there. With `CFS_CLAP_TRACE=1`: after the restart, `%TEMP%\cfs_clap_trace.txt` has a line
     `activate: latency 0 -> 37 samples`, and the audio summary ends with `restart requests 1`.
4. Toggle **Drive on** while a chord holds, with Oversample 4x.
   - Expect: a smooth 10 ms fade, **no restart** (switching never changes the latency) and no flam or comb.
5. **Tone**: Drive Soft 24 dB, sweep **Tone** from −12 to +12 dB on a chord.
   - Expect: −12 = fat, the highs stay clean; +12 = fizzy, the highs distort first. With Drive at 0 dB and Type
     Hard on a quiet sound, Tone should change nothing at all (it's an exact pre/post pair).
6. **Crush on the bell**: load **Bell**, switch **Crush on** (Bits 8, Rate 12000 Hz). Then Bits 6 → 4, Rate down
   to 6000 and 3000 Hz.
   - Expect: bits add a grainy noise that follows the bell's decay (silent when it's silent); rate makes new,
     inharmonic metallic tones (aliasing on purpose). Report: musical, or just broken?
7. CPU: Drive at 4x costs about 1 % of a core (all voices together); Crush almost nothing.

### L5.5: the reverb on the bell
1. Load **Bell** and play a few notes (C5–C6), then load **Bell hall** (factory preset 14) and play the same.
   - Expect: the same bell in a hall: a short gap (20 ms), then a smooth, wide wash that lasts about 3 s,
     its highs fading before its lows, with no flutter or metallic rattle.
   - Report: would you use it on your FM bells instead of FL's own reverb (Fruity Reeverb 2)? Too bright, too dark,
     too long? (Try **Damping** 20–70 %, **Size** 80–160 %, **Decay** 1.5–6 s, **Mix** 20–40 %.)
2. **Decay = seconds**: set Decay to **2 s**, Mix 100 %, play one short staccato note and count: the tail should
   be inaudible after roughly 2–3 s (RT60 is the time to fall 60 dB; it's measured at 2.00 s at 1 kHz).
3. Switch **Reverb on** off while the tail rings.
   - Expect: the tail **rings out** (spill, like the delay); new notes are dry.
4. Turn **Size** while a long tail rings: the tail bends in pitch briefly (the line lengths glide), no clicks.
5. **Mono**: put CFS's mixer track in mono with Bell hall.
   - Expect: the reverb gets narrower and a little quieter, no hollow or phasey sound (the tail's L/R
     correlation measures 0.03: almost independent channels).
6. Render with a long Decay (10 s): the export should contain the whole tail (clap.tail grows with the decay).
7. CPU: the reverb costs about 1.3 % of a core, once for the whole instrument (it's after the voices).

### L4.2: operator waves, a supersaw, FM 2x
1. Load **Supersaw** (factory preset 16) and hold a chord, on headphones.
   - Expect: seven detuned saws per note, wide and bright with a gentle hall: the trance/JP-8000 sound.
   - Report: does it sound like a supersaw to you? (Try **Unison detune** 25–60 cents, **Unison** 5–8, the filter
     **Cutoff** 3–10 kHz.) Watch FL's CPU meter on a big chord: 7 copies cost ~4–5× a plain patch.
2. Mono: put CFS's mixer track in mono with Supersaw.
   - Expect: narrower and about 1.6 dB quieter, no hollow sound.
3. Load **Saw lead** (15) and play a monophonic line, overlapping notes.
   - Expect: a classic resonant saw lead, legato with a short glide, the filter opening on each new (detached) note,
     vibrato arriving after ~0.3 s.
4. Load **Wave bass** (17) and **Wave clav** (18): FM made from the TX81Z-style waves.
   - Report: do they sound "4-op Yamaha" to you? Too thin, too clicky?
5. On any patch, open the Synth page and change an operator's **Wave** while holding a note (e.g. Init, Operator D:
   Sine → Saw → Square → Abs sine).
   - Expect: the timbre switches at once (a menu: a small step is normal). **Abs sine** and **Quarter sine** sound
     an octave higher than Sine; no thump on note on/off with any wave.
6. **Ratio snapping**: drag an operator's **Ratio** knob: it steps through 1/8, 1/4, 1/2, 1, 2, 3 ... 32. Hold
   **Ctrl** while dragging: it moves freely (inharmonic ratios for bells). Automation and values set from FL are
   never snapped (the snapping is only the knob's).
7. **FM 2x** (Synth page, Voice section): load **Brass**, play C6–C8 hard, then switch **FM 2x** on.
   - Expect: the highest notes lose a faint inharmonic whistle (aliasing), the rest sounds the same. CFS asks FL
     for a restart (latency +16 samples, like the drive's oversampling in L5.2): report if anything glitches or if
     the switch only takes effect after you press play again.
8. Matrix amounts no longer step: on the Mod page, slot 1 = LFO 2 → Pitch, then automate its **Amount** with a fast
   FL automation clip. Report whether the zipper from the L4.1 check is gone.
