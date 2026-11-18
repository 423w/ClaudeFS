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
4. FL's own parameter list: in the wrapper menu, **Browse parameters**. Expect 82 parameters (30 global + 13 per operator), grouped with
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
