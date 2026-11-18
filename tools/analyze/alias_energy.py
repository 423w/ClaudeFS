"""Split a steady tone's energy into harmonic and non-harmonic (alias + noise) parts.

    python tools/analyze/alias_energy.py renders/l1.8_tone_saw_naive_85.wav 996.09375
    python tools/analyze/alias_energy.py renders/l1.8_tone_saw_blep_*.wav --f0 auto --below 5000

How it works:
  1. Take the samples (default: the whole file), multiply by a 4-term Blackman-Harris window, FFT.
     Blackman-Harris sidelobes are ~92 dB down, so a loud harmonic's leakage doesn't pollute the bins
     where we look for quiet aliases (a Hann window's skirts would read as fake "aliasing" at -60 dB or so).
  2. Mark +/- `--guard` bins around every harmonic h*f0 below Nyquist as "harmonic". The window's main
     lobe is 4 bins wide each side, so the default guard of 6 covers it plus a little frequency error.
  3. harmonic energy = sum of |X|^2 in marked bins; non-harmonic = everything else except DC.
     Report both relative to the total, in dB. --below F also reports the non-harmonic part under F Hz.

Unlike fss_render's exact C++ meter (tools/render/alias_meter.h), this works on ANY steady tone, at any
f0, at the price of a noise floor set by the window (~-90..-100 dB for a full-scale saw). Use it to check
the C++ numbers, or on a tone from another synth. --f0 auto estimates f0 from the strongest peak.
"""
from __future__ import annotations

import argparse
import glob
import re

import numpy as np
from scipy import signal as sps

import wavio


def analyse(x: np.ndarray, rate: int, f0: float, guard: int, below: float | None) -> dict[str, float]:
    w = sps.get_window("blackmanharris", len(x))
    P = np.abs(np.fft.rfft(x * w)) ** 2  # |X|^2; the window's scale cancels in the ratios below
    freqs = np.fft.rfftfreq(len(x), 1.0 / rate)
    bin_hz = freqs[1]

    harmonic = np.zeros(len(P), dtype=bool)
    h = 1
    while h * f0 < rate / 2:
        c = int(round(h * f0 / bin_hz))
        harmonic[max(c - guard, 0):c + guard + 1] = True
        h += 1
    dc = np.zeros(len(P), dtype=bool)
    dc[:guard + 1] = True  # DC and its window lobe are neither harmonic nor alias
    harmonic &= ~dc

    total = P[~dc].sum()
    other = ~harmonic & ~dc
    out = {
        "harmonics": h - 1,
        "harmonic_db": 10 * np.log10(P[harmonic].sum() / total),
        "alias_db": 10 * np.log10(max(P[other].sum(), 1e-300) / total),
    }
    if below:
        out["alias_below_db"] = 10 * np.log10(max(P[other & (freqs < below)].sum(), 1e-300) / total)
    return out


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", nargs="+", help="WAV file(s); globs are expanded")
    p.add_argument("--f0", default=None,
                   help="fundamental in Hz, or 'auto'. Default: from a '_<k>.wav' suffix (f0 = k*fs/4096, "
                        "fss_render's l1.8 naming), else auto")
    p.add_argument("--start", type=float, default=0.0, help="seconds")
    p.add_argument("--duration", type=float, default=None, help="seconds (default: whole file)")
    p.add_argument("--guard", type=int, default=6, help="bins either side of each harmonic counted as harmonic")
    p.add_argument("--below", type=float, default=None, help="also report non-harmonic energy below this Hz")
    args = p.parse_args()

    paths = [q for pattern in args.wav for q in (sorted(glob.glob(pattern)) or [pattern])]
    print(f"{'file':<44} {'f0 (Hz)':>10} {'harm':>5} {'harmonic dB':>12} {'alias dB':>9}"
          + (f" {'<' + str(int(args.below)) + ' Hz':>9}" if args.below else ""))
    for path in paths:
        rate, x = wavio.load_wav(path)
        x = wavio.slice_seconds(x, rate, args.start, args.duration)
        if args.f0 and args.f0 != "auto":
            f0 = float(args.f0)
        elif args.f0 is None and (m := re.search(r"_(\d+)\.wav$", path)):
            f0 = int(m.group(1)) * rate / 4096
        else:  # strongest peak; fine for saw/square/triangle, whose fundamental is the loudest partial
            P = np.abs(np.fft.rfft(x * sps.get_window("blackmanharris", len(x))))
            f0 = np.argmax(P[1:]) + 1
            f0 *= rate / len(x)
        r = analyse(x, rate, f0, args.guard, args.below)
        name = path.replace("\\", "/").split("/")[-1]
        line = f"{name:<44} {f0:10.2f} {r['harmonics']:5d} {r['harmonic_db']:12.3f} {r['alias_db']:9.1f}"
        if args.below:
            line += f" {r['alias_below_db']:9.1f}"
        print(line)


if __name__ == "__main__":
    main()
