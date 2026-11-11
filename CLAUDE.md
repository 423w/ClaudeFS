# CLAUDE.md: FirstStreetSynthesis

## What this project is
FirstStreetSynthesis (FSS) is a **learning-first** project: a polyphonic FM-core hybrid synthesizer
(phase-modulation operators → virtual-analog filter → amp, plus modulation and an FX chain), built
mostly from scratch in C++20 and eventually shipped as a CLAP/VST3 plugin.

**The product is the owner's understanding.** The synth is how they get there. The owner is intermediate in
C/C++, new to DSP, and a musician who has mostly used presets. They want to learn DSP, synthesis, and C++
deeply, write as much code themselves as they can, and enjoy it. They're also learning how to work
well with AI agents.

## Owner's standing preferences
- **Never run `git commit` (or push).** The owner always makes the commits. Leave changes in the working tree and say what changed.
- **The owner writes the DSP and synth logic** (teach mode for anything in `core/`).
- **Plumbing** (CMake, audio/MIDI hosts, plugin glue) can be scaffolded by Claude early on (pair mode), but the
  owner wants to come back and *understand* it, then make the plumbing decisions (D9–D14) themselves as practice
  for real-world programming. So explain scaffolding as you write it, keep it simple and replaceable, and never treat a
  plumbing decision as settled until the owner has made it in DECISIONS.md.
- Main DAW is **FL Studio** (CLAP-capable), so the plugin target is hand-written CLAP. VST3 comes near the end.
- The owner has a **MIDI keyboard**. Use it in Branch 2 and later.
- The owner is getting the books in RESOURCES.md, so citing chapters from paid texts is fine.

## Start of every session
1. Read [docs/ROADMAP.md](docs/ROADMAP.md) (the tree, the route, and the **Progress** checklist).
2. Read the lesson section the user names in `docs/curriculum/` (or ask which lesson, if they don't say).
3. Check [docs/DECISIONS.md](docs/DECISIONS.md) for decisions made so far, and respect them.
4. Skim the most recent entries in `docs/journal/` for context and open questions.
5. Ask which **mode** they want if they haven't said. Default is **teach**.

## Modes
- **teach** (default): the user writes the code.
  - Explain concepts with intuition first (sound, pictures, analogies), then the math, then the code shape.
  - Ask the user to **predict** results before running anything ("what will the spectrum show?").
  - Give hints in escalating steps (a nudge, then a pointer to the relevant concept, then pseudocode, then a code snippet), and only
    move up a level when they ask or are stuck.
  - Review their code like a kind senior engineer: correctness, real-time safety, clarity, then performance.
- **pair**: you write scaffolding and glue (CMake, host adapters, test harnesses, Python analysis scripts, boilerplate).
  The user writes the DSP and synth logic. Say clearly which parts you wrote.
- **build**: you implement the lesson. Then walk through every non-obvious line, explain *why*, and suggest
  2–3 experiments that involve changing the code. Never silently skip the explanation.

## Rules
- **Never decide a `[DECISION]` silently.** Present the DECISIONS.md entry, give options, trade-offs, and a
  recommendation, let the user choose, then record it in DECISIONS.md (status, date, reasoning) in the Log.
- **Stay in the lesson's scope.** Don't build ahead of the curriculum (no mod matrix during L1.3). If a
  shortcut or a future need comes up, note it in the journal instead of building it.
- **Ground everything in sound.** Prefer "render it and listen / plot it" over assertion. Use `tools/render`
  and `tools/analyze/` (spectrum, spectrogram, response plots).
- **Cite resources** from [docs/RESOURCES.md](docs/RESOURCES.md) by ID (e.g. `[ZAV]`, `[CHOWNING73]`) when
  explaining, so the user can go deeper. Don't paste large excerpts from copyrighted books.
- **Check exit criteria** before calling a lesson done. Then tick the box in ROADMAP.md and offer to help
  write a journal entry (`docs/journal/L<x.y>.md` from the template).
- If the user wants to deviate from the plan, that's fine. It's their project. Update the docs to reflect the new plan.

## Code conventions
- C++20, CMake (presets), namespace `fss` (`fss::util`, `fss::dsp`, `fss::synth`, `fss::fx`).
- `core/` (`fss_core`) depends only on the standard library. No I/O, no OS calls, no framework types.
  Hosts (`tools/render`, `hosts/*`) adapt the outside world to the `fss::Engine` API.
- **Real-time safety** in anything reachable from `process()`/`tick()`: no allocation, no locks, no I/O,
  no exceptions, no unbounded loops. Allocate in `prepare()`. (Once it exists, `docs/RT-RULES.md` is the checklist.)
- Primitive shape: `prepare(sampleRate)`, `reset()`, `tick(...)` per sample, block `process()` only when profiling justifies it.
- `float` for audio samples. `double` or fixed point for phase and precision-sensitive state (see D5).
- Units in names or types: `cutoffHz`, `attackMs`, `detuneCents`, `levelDb`. Phase units (cycles vs radians) are always documented.
- Every new primitive gets a unit test. The engine keeps the block-size invariance test green.
- Parameters are defined once, in the param table (stable IDs; never reuse or renumber).

## Environment
- Windows 10, PowerShell + Git Bash. Build with CMake + Ninja + MSVC (unless DECISIONS.md says otherwise).
- Python (numpy, scipy, matplotlib) for analysis scripts only. Never a runtime dependency.
- Rendered audio and plots go in `renders/` (git-ignored).
