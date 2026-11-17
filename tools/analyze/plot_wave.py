"""Plot a WAV's waveform (amplitude vs time).

    python tools/analyze/plot_wave.py renders/foo.wav
    python tools/analyze/plot_wave.py renders/foo.wav --start 0.5 --duration 0.01   # zoom to a few cycles

When the selection is short enough to see individual samples (< 400), each one is drawn as a dot. That's
the digital truth: there is nothing *between* the dots, only what a DAC will reconstruct.
"""
from __future__ import annotations

import argparse

import matplotlib

import numpy as np

import wavio


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav")
    p.add_argument("--start", type=float, default=0.0, help="seconds")
    p.add_argument("--duration", type=float, default=None, help="seconds (default: to the end)")
    p.add_argument("--channel", default="mix", help='"mix" or a channel index (0 = left)')
    p.add_argument("--out", help="output PNG (default renders/<name>.wave.png)")
    p.add_argument("--show", action="store_true", help="also open a window")
    args = p.parse_args()

    if not args.show:
        matplotlib.use("Agg")  # no window: render straight to file (works in terminals and CI)
    import matplotlib.pyplot as plt

    channel = args.channel if args.channel == "mix" else int(args.channel)
    rate, x = wavio.load_wav(args.wav, channel)
    x = wavio.slice_seconds(x, rate, args.start, args.duration)
    t = args.start + np.arange(len(x)) / rate

    fig, ax = plt.subplots(figsize=(11, 4))
    if len(x) < 400:
        ax.plot(t, x, "-", lw=0.8, alpha=0.5)
        ax.plot(t, x, "o", ms=3)
    else:
        ax.plot(t, x, lw=0.6)
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    ax.set_title(f"{args.wav}  ({rate} Hz, peak {peak:.3f} = {20 * np.log10(max(peak, 1e-12)):.1f} dBFS)")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("amplitude (1.0 = full scale)")
    ax.set_ylim(-1.05, 1.05)
    ax.grid(alpha=0.3)
    wavio.finish(fig, wavio.output_path(args.wav, "wave", args.out), args.show)


if __name__ == "__main__":
    main()
