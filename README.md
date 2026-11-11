# FirstStreetSynthesis

An FM synthesizer built for me to learn DSP, agentic orchestration, and C/C++.

The plan is a polyphonic **FM-core hybrid** synth: phase-modulation operators feed a virtual-analog filter, then an amp,
with envelopes, LFOs, a mod matrix, and a built-in effects chain. It's written mostly from scratch in C++20
and grows from an offline WAV renderer into a real-time standalone app and then a CLAP/VST3 plugin.

## Start here
- **[docs/ROADMAP.md](docs/ROADMAP.md)**: the lesson tree, the suggested route, and progress.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): where the code is heading.
- [docs/DECISIONS.md](docs/DECISIONS.md): open and made design decisions.
- [docs/RESOURCES.md](docs/RESOURCES.md): books, papers, code, and tools.
- [docs/curriculum/](docs/curriculum/): the lessons.
- [CLAUDE.md](CLAUDE.md): how Claude sessions should tutor and work in this repo.
