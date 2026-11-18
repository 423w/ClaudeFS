"""Stack the spectrograms of several WAVs into one PNG, same axes and colour scale, for side-by-side comparison.

    python tools/analyze/compare_spectrograms.py renders/l1.8_sweep_saw_naive.wav \
        renders/l1.8_sweep_saw_blep.wav renders/l1.8_sweep_saw_additive.wav --out renders/l1.8_saw_3way.png

Same calibration as spectrogram.py (a full-scale sine reads ~0 dBFS) and a *linear* frequency axis, where
aliases are easiest to spot: lines that go DOWN while the pitch goes up, or bounce off 0 Hz and Nyquist.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib
import numpy as np
from scipy import signal as sps

import wavio


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", nargs="+")
    p.add_argument("--nfft", type=int, default=2048)
    p.add_argument("--floor", type=float, default=-120.0, help="dB shown as the darkest colour")
    p.add_argument("--ceiling", type=float, default=-10.0, help="dB shown as the brightest colour")
    p.add_argument("--out", required=True, help="output PNG")
    args = p.parse_args()

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(len(args.wav), 1, figsize=(11, 3.2 * len(args.wav)), sharex=True, squeeze=False)
    mesh = None
    for ax, path in zip(axes[:, 0], args.wav):
        rate, x = wavio.load_wav(path)
        win = sps.get_window("hann", args.nfft)
        f, t, S = sps.spectrogram(x, fs=rate, window=win, nperseg=args.nfft, noverlap=args.nfft * 3 // 4,
                                  mode="magnitude", scaling="spectrum")
        S_db = 20.0 * np.log10(np.maximum(2.0 * S, 1e-12))
        mesh = ax.pcolormesh(t, f / 1000.0, S_db, vmin=args.floor, vmax=args.ceiling, shading="auto", cmap="magma")
        ax.set_ylabel("kHz")
        ax.set_title(Path(path).name, fontsize=10)
    axes[-1, 0].set_xlabel("time (s)")
    fig.colorbar(mesh, ax=axes[:, 0].tolist(), label="dBFS", shrink=0.8)
    wavio.finish(fig, Path(args.out), False)


if __name__ == "__main__":
    main()
