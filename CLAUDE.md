# CLAUDE.md: CFS (Claude FS)

## What this project is
CFS is **Claude's own build** of the FirstStreetSynthesis synthesizer: a polyphonic FM-core hybrid
(phase-modulation operators → virtual-analog filter → amp, plus modulation and an FX chain), written in C++20
and eventually shipped as a CLAP plugin (VST3 near the end).

It is a fork of the owner's repo, **FirstStreetSynthesis (FSS)**, which lives at
`C:\Users\wes\Desktop\XP\FirstStreetSynthesis`. In FSS the owner writes the code to learn DSP, synthesis, and C++.
In CFS **Claude writes the code**, following the same roadmap, so the owner can put the two side by side, lesson by
lesson, for inspiration, ideas, and feedback.

**The product is still the owner's understanding.** CFS is a reference implementation and a sparring partner, not a
replacement for their work. Write everything so a curious intermediate C++ programmer who is new to DSP can read it,
compare it with their own version, and learn from the differences.

## Standing rules
- **Claude develops here autonomously.** No teach/pair modes: Claude implements each lesson itself (what FSS calls
  **build** mode), including DSP, synth logic, and plumbing.
- **Commits: Claude commits in CFS, one commit per lesson** (owner's decision, 2026-11-17). When a lesson's exit
  criteria are met, commit its code, ROADMAP tick, DECISIONS entries, and journal entry together, with a message
  like `L1.2: phase accumulator & sine oscillator`. Small fixes between lessons may get their own commits. This
  applies **only to CFS**: never commit in the owner's FSS repo.
- **Remotes.** `origin` is CFS's own GitHub repo (`423w/ClaudeFS`), the only remote that accepts pushes. Don't push
  unless the owner asks. `upstream` (the owner's FSS GitHub repo) and `fss-local` (their local FSS checkout) have
  their push URLs deliberately disabled; they're for fetching or comparing only. Never re-enable them.
- **Never modify the owner's FSS repo.** Don't read FSS's source code for implementation ideas unless the owner asks:
  CFS is most useful as an *independent* take. Reading FSS's `docs/` is fine.
- Main DAW is **FL Studio** (CLAP-capable). The owner has a **MIDI keyboard** for testing Branch 2 and later.

## Start of every session
1. Read [docs/ROADMAP.md](docs/ROADMAP.md): the tree, the route, and the **Progress** checklist (it tracks CFS's progress, not the owner's).
2. Pick up the next lesson on the route (or the one the owner names) and read its section in `docs/curriculum/`.
3. Check [docs/DECISIONS.md](docs/DECISIONS.md) for decisions made so far in CFS, and respect them.
4. Skim the most recent entries in `docs/journal/` for context and open questions.

## How to work a lesson
- **Same route, same lesson numbers** as FSS, so the owner can compare L1.2 with L1.2. Stay in the lesson's scope;
  don't build ahead (no mod matrix during L1.3). Note future ideas in the journal instead.
- **Ground everything in sound.** Render it, plot it, measure it. Use `tools/render` and `tools/analyze/`, and put
  outputs in `renders/` (git-ignored).
- **Meet the exit criteria** before calling a lesson done. Then tick it in ROADMAP.md and write the journal entry.
- **Explain in the code and the journal, not just in chat.** Chat disappears; the owner will read this repo later.
  Comment the *why* of every non-obvious line. Cite [docs/RESOURCES.md](docs/RESOURCES.md) by ID (e.g. `[ZAV]`).

## Decisions
- **Claude makes the `[DECISION]` calls in CFS**, plumbing decisions (D9–D14) included. That's the point: the owner
  wants to see what Claude would choose and why, then compare with their own choice.
- Record each one in DECISIONS.md's Log: the decision, options considered, why, what would make you revisit, and
  **what the owner might reasonably choose differently**.
- Decisions the owner already made for the project (D2: FL Studio, hand-written CLAP first) still bind CFS.

## Journal (one entry per lesson)
Write `docs/journal/L<x.y>.md` from [docs/journal/TEMPLATE.md](docs/journal/TEMPLATE.md) at the end of each lesson.
The most valuable sections for the owner: **alternatives rejected**, **what surprised me**, **compare with your
version**, and **experiments to try**.

## Code conventions
- C++20, CMake (presets), namespace `fss` (`fss::util`, `fss::dsp`, `fss::synth`, `fss::fx`). Keeping the same
  namespaces and file layout as FSS makes side-by-side comparison easy.
- `core/` (`fss_core`) depends only on the standard library. No I/O, no OS calls, no framework types.
  Hosts (`tools/render`, `hosts/*`) adapt the outside world to the `fss::Engine` API.
- **Real-time safety** in anything reachable from `process()`/`tick()`: no allocation, no locks, no I/O,
  no exceptions, no unbounded loops. Allocate in `prepare()`. **[docs/RT-RULES.md](docs/RT-RULES.md) is the checklist**,
  and `fss::util::RealtimeGuard` + the `fss_rt_trap` allocation trap enforce the allocation rule in tests and Debug hosts.
- Primitive shape: `prepare(sampleRate)`, `reset()`, `tick(...)` per sample, block `process()` only when profiling justifies it.
- `float` for audio samples. `double` or fixed point for phase and precision-sensitive state (see D5).
- Units in names or types: `cutoffHz`, `attackMs`, `detuneCents`, `levelDb`. Phase units (cycles vs radians) are always documented.
- Every new primitive gets a unit test. The engine keeps the block-size invariance test green.
- Parameters are defined once, in the param table (stable IDs; never reuse or renumber).
- Text that reaches a Windows console or command line (printed output, CTest/Catch2 test names) is ASCII.
  Unicode (→, −, ×) is fine in comments, docs and file contents. (Learned the hard way in L0.2–L1.1.)

## Environment
- Windows 10, PowerShell + Git Bash. Build with CMake + Ninja + MSVC (unless DECISIONS.md says otherwise).
- Python (numpy, scipy, matplotlib) for analysis scripts only. Never a runtime dependency.
- Rendered audio and plots go in `renders/` (git-ignored).
