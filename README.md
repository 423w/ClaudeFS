# CFS (Claude FS)

Claude's own build of **FirstStreetSynthesis**, an FM-core hybrid synthesizer in C++20.

The owner is building FirstStreetSynthesis (FSS) by hand to learn DSP, synthesis, and C++. CFS follows the
same roadmap and lesson numbers, but here Claude writes all the code and makes all the design decisions, so the two
can be compared side by side for inspiration, ideas, and feedback.

**Suggested way to compare:** finish a lesson in FSS first, then read the same lesson here (its code, its
entry in [docs/DECISIONS.md](docs/DECISIONS.md), and its journal entry in [docs/journal/](docs/journal/)).
Reading ahead is allowed, but it spoils the puzzles.

## Build and test (Windows, VS 2022)
```powershell
. .\tools\devshell.ps1                 # puts cl, cmake, ninja on PATH (once per PowerShell session)
cmake --workflow --preset release      # configure + build + test (or: debug presets for stepping)
python tools\analyze\plot_spectrum.py renders\some.wav --peaks 10   # also: plot_wave.py, spectrogram.py
```
The first configure downloads Catch2 (pinned by hash) into `build/`. Plots and renders go in `renders/` (git-ignored).

## Start here
- **[docs/ROADMAP.md](docs/ROADMAP.md)**: the lesson tree, the route, and CFS's progress.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): where the code is heading.
- [docs/DECISIONS.md](docs/DECISIONS.md): the decisions Claude made, and why.
- [docs/RESOURCES.md](docs/RESOURCES.md): books, papers, code, and tools.
- [docs/curriculum/](docs/curriculum/): the lessons.
- [CLAUDE.md](CLAUDE.md): how Claude sessions work in this repo.
