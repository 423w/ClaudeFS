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
.\build\release\tools\render\fss_render.exe   # render the current lesson's sounds into renders\
.\build\release\hosts\play\fss_play.exe --exclusive --period 64   # THE synth: GUI, mouse/QWERTY/MIDI, every parameter (L2.4)
.\build\release\hosts\standalone\fss_standalone.exe --keys --exclusive --period 64 --seconds 60   # console host (L2.1-L2.3)
python tools\analyze\plot_spectrum.py renders\some.wav --peaks 10   # also: plot_wave.py, spectrogram.py
.\build\release\hosts\clap\fss_clap_host.exe .\build\release\hosts\clap\CFS.clap --bench   # headless CLAP host checks (L3.2+)
.\tools\install_clap.ps1               # (elevated) copy CFS.clap where FL Studio finds it: see docs/FL-STUDIO.md
```
The first configure downloads the pinned dependencies (Catch2, miniaudio, Dear ImGui, the CLAP SDK, clap-validator)
into `build/`. Plots and renders go in `renders/` (git-ignored). Factory presets are text files in `presets/factory/`.

## Start here
- **[docs/ROADMAP.md](docs/ROADMAP.md)**: the lesson tree, the route, and CFS's progress.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): where the code is heading.
- [docs/DECISIONS.md](docs/DECISIONS.md): the decisions Claude made, and why.
- [docs/RESOURCES.md](docs/RESOURCES.md): books, papers, code, and tools.
- [docs/curriculum/](docs/curriculum/): the lessons.
- [CLAUDE.md](CLAUDE.md): how Claude sessions work in this repo.
