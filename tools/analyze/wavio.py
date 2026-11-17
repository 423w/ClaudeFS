"""Shared helpers for the analysis scripts: load any WAV as float, and save or show a figure.

Analysis tooling only (CLAUDE.md): numpy/scipy/matplotlib are never runtime dependencies of the synth.
"""
from __future__ import annotations

import os
from pathlib import Path

import numpy as np
from scipy.io import wavfile

# Repo root = two levels above tools/analyze/. Plots default to <repo>/renders/ (git-ignored).
REPO_ROOT = Path(__file__).resolve().parents[2]
RENDERS = REPO_ROOT / "renders"


def load_wav(path: str | os.PathLike, channel: str | int = "mix") -> tuple[int, np.ndarray]:
    """Load a WAV file and return (sample_rate, samples) with samples as float64 in [-1, 1).

    WAV files store samples as unsigned 8-bit, signed 16/24/32-bit integers, or 32/64-bit floats. To compare
    levels across files we map all of them to the same scale, where 1.0 is digital full scale (0 dBFS).
    channel: "mix" averages all channels to mono; an int picks one channel (0 = left).
    """
    rate, data = wavfile.read(path)
    if data.dtype == np.uint8:
        # 8-bit WAV is the odd one out: unsigned, with silence at 128 instead of 0.
        x = (data.astype(np.float64) - 128.0) / 128.0
    elif np.issubdtype(data.dtype, np.integer):
        # int16 → divide by 2^15, int32 → 2^31. (scipy returns 24-bit files as int32, left-justified,
        # so 2^31 is right for those too.)
        x = data.astype(np.float64) / float(-np.iinfo(data.dtype).min)
    else:
        x = data.astype(np.float64)  # float WAVs are already on the ±1.0 scale

    if x.ndim == 2:
        x = x.mean(axis=1) if channel == "mix" else x[:, int(channel)]
    return int(rate), x


def slice_seconds(x: np.ndarray, rate: int, start: float = 0.0, duration: float | None = None) -> np.ndarray:
    """Return the part of x from `start` seconds, `duration` seconds long (None = to the end)."""
    a = int(round(start * rate))
    b = len(x) if duration is None else min(len(x), a + int(round(duration * rate)))
    if a >= b:
        raise SystemExit(f"empty selection: start={start}s, duration={duration}s, file is {len(x) / rate:.3f}s")
    return x[a:b]


def output_path(wav_path: str | os.PathLike, suffix: str, out: str | None) -> Path:
    """Where to save a plot: --out if given, else renders/<wav name>.<suffix>.png."""
    if out:
        return Path(out)
    RENDERS.mkdir(exist_ok=True)
    return RENDERS / f"{Path(wav_path).stem}.{suffix}.png"


def finish(fig, path: Path, show: bool) -> None:
    """Save the figure (always) and optionally open a window."""
    fig.savefig(path, dpi=120, bbox_inches="tight")
    print(f"wrote {path}")
    if show:
        import matplotlib.pyplot as plt
        plt.show()
