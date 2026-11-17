"""Print level and brightness (spectral centroid) over time for one or more WAVs.

    python tools/analyze/brightness.py renders/foo.wav [--step 0.1] [--times 0.05,0.3,1.0]

Spectral centroid = the "centre of mass" of the magnitude spectrum, sum(f·|X|)/sum(|X|), in Hz. It's the
simplest single number for "how bright" a sound is: a pure sine's centroid is its frequency, and every
added upper partial pulls it up. Level is the RMS of the slice in dBFS (a full-scale sine reads −3 dB).

Used in L1.4 (brass gets brighter as it gets louder, the bell gets darker as it decays) and L1.6 (key scaling).
"""
from __future__ import annotations

import argparse

import numpy as np

import wavio


def slice_stats(x: np.ndarray, rate: int) -> tuple[float, float]:
    rms = float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0
    w = np.hanning(len(x))
    mag = np.abs(np.fft.rfft(x * w))
    f = np.fft.rfftfreq(len(x), 1 / rate)
    centroid = float(np.sum(f * mag) / np.sum(mag)) if np.sum(mag) > 0 else 0.0
    return 20 * np.log10(max(rms, 1e-12)), centroid


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", nargs="+")
    p.add_argument("--step", type=float, default=0.1, help="seconds between slices (if --times not given)")
    p.add_argument("--times", help="comma-separated slice centres in seconds")
    p.add_argument("--window", type=float, default=0.04, help="slice length in seconds")
    args = p.parse_args()

    for path in args.wav:
        rate, x = wavio.load_wav(path)
        dur = len(x) / rate
        times = ([float(t) for t in args.times.split(",")] if args.times
                 else list(np.arange(args.window / 2, dur - args.window / 2, args.step)))
        print(f"\n{path}")
        print(f"{'t (s)':>8} {'level dBFS':>11} {'centroid Hz':>12}")
        h = int(args.window * rate / 2)
        for t in times:
            c = int(t * rate)
            level, cent = slice_stats(x[max(c - h, 0):c + h], rate)
            print(f"{t:8.3f} {level:11.1f} {cent:12.0f}")


if __name__ == "__main__":
    main()
