"""Plot a spectrogram (frequency vs time, colour = level in dBFS) of a WAV.

    python tools/analyze/spectrogram.py renders/foo.wav
    python tools/analyze/spectrogram.py renders/foo.wav --log --nfft 4096

The best aliasing detector there is. Use the default *linear* frequency axis for that: a real partial
sweeping up draws a line going up; when it passes Nyquist (fs/2) its alias appears as a line coming back
DOWN, a "mirror" that shouldn't exist. Log axes make that reflection much harder to see.

Trade-off: --nfft sets the window length N. Bigger N = finer frequency resolution (fs/N Hz) but blurrier
timing (N/fs seconds per frame). 2048 at 48 kHz ≈ 23 Hz × 43 ms. Frames overlap 75% to smooth the time axis.
"""
from __future__ import annotations

import argparse

import matplotlib
import numpy as np
from scipy import signal as sps

import wavio


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav")
    p.add_argument("--start", type=float, default=0.0, help="seconds")
    p.add_argument("--duration", type=float, default=None, help="seconds (default: whole file)")
    p.add_argument("--channel", default="mix", help='"mix" or a channel index (0 = left)')
    p.add_argument("--nfft", type=int, default=2048, help="window length in samples")
    p.add_argument("--floor", type=float, default=-120.0, help="dB shown as the darkest colour")
    p.add_argument("--log", action="store_true", help="log frequency axis (pretty, worse for aliasing)")
    p.add_argument("--out", help="output PNG (default renders/<name>.spectrogram.png)")
    p.add_argument("--show", action="store_true", help="also open a window")
    args = p.parse_args()

    if not args.show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    channel = args.channel if args.channel == "mix" else int(args.channel)
    rate, x = wavio.load_wav(args.wav, channel)
    x = wavio.slice_seconds(x, rate, args.start, args.duration)
    nfft = min(args.nfft, len(x))

    # mode="magnitude" returns |X|. Same calibration as plot_spectrum: divide by sum(window)/2 so a
    # full-scale sine reads ~0 dBFS (scipy's default "density" scaling would give power per Hz instead).
    win = sps.get_window("hann", nfft)
    f, t, S = sps.spectrogram(x, fs=rate, window=win, nperseg=nfft, noverlap=nfft * 3 // 4,
                              mode="magnitude", scaling="spectrum")
    # scipy's "spectrum" scaling already divides by sum(win); the ×2 restores the half of each sine's
    # energy that lives in the negative frequencies.
    S_db = 20.0 * np.log10(np.maximum(2.0 * S, 1e-12))

    fig, ax = plt.subplots(figsize=(11, 5))
    mesh = ax.pcolormesh(t + args.start, f, S_db, vmin=args.floor, vmax=0.0, shading="auto", cmap="magma")
    if args.log:
        ax.set_yscale("log")
        ax.set_ylim(20.0, rate / 2)
    ax.set_xlabel("time (s)")
    ax.set_ylabel("frequency (Hz)")
    ax.set_title(f"{args.wav}: nfft {nfft} ({rate / nfft:.1f} Hz × {1000 * nfft / rate:.0f} ms)")
    fig.colorbar(mesh, ax=ax, label="dBFS")
    wavio.finish(fig, wavio.output_path(args.wav, "spectrogram", args.out), args.show)


if __name__ == "__main__":
    main()
