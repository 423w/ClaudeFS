"""Measure the click at the start and end of a WAV: high-frequency energy at the edges.

    python tools/analyze/click_meter.py renders/foo.wav [--cutoff 2000]

A player goes silence → file → silence. Any jump in the *value* (or the slope) at those seams is a
broadband burst, a click, even if the tone itself is a pure low sine. To measure it, splice silence onto
each edge, take a Hann-windowed FFT centred on the seam, and sum the energy above --cutoff Hz, where a
low tone has (almost) nothing of its own. Reported in dB relative to a full-scale sine's energy, so
lower is cleaner. Compare files with each other, not with an absolute threshold.

L1.1 uses it to explain clicks; L1.3 uses it to show envelopes remove them.
"""
from __future__ import annotations

import argparse

import numpy as np

import wavio


def edge_energy_db(seam: np.ndarray, rate: int, cutoff: float) -> float:
    w = np.hanning(len(seam))
    X = np.fft.rfft(seam * w) / (np.sum(w) / 2)
    f = np.fft.rfftfreq(len(seam), 1 / rate)
    e = np.sum(np.abs(X[f >= cutoff]) ** 2)
    return 10 * np.log10(max(e, 1e-24))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", nargs="+")
    p.add_argument("--cutoff", type=float, default=2000.0, help="Hz; energy above this counts as click")
    p.add_argument("--half", type=int, default=512, help="samples on each side of the seam")
    p.add_argument("--channel", default="mix",
                   help="'mix' (average, the default) or a channel number (0 = left). A stereo-only change, like "
                        "L5.1's width switch, is invisible in the mix: measure one channel")
    p.add_argument("--at", type=float, action="append", default=[],
                   help="also measure a window centred at this time in seconds (e.g. a note-off); repeatable")
    args = p.parse_args()

    at_cols = "".join(f" {'@' + format(t, 'g') + 's':>9}" for t in args.at)
    print(f"{'file':44} {'first':>7} {'last':>8} {'start dB':>9} {'end dB':>8}{at_cols}"
          f"   (energy above {args.cutoff:g} Hz)")
    for path in args.wav:
        rate, x = wavio.load_wav(path, args.channel if args.channel == "mix" else int(args.channel))
        h = min(args.half, len(x))
        z = np.zeros(h)
        start = np.concatenate([z, x[:h]])
        end = np.concatenate([x[-h:], z])
        cols = ""
        for t in args.at:
            c = int(round(t * rate))
            seg = x[max(c - h, 0):c + h]
            cols += f" {edge_energy_db(seg, rate, args.cutoff):9.1f}"
        print(f"{path:44} {x[0]:7.3f} {x[-1]:8.3f} {edge_energy_db(start, rate, args.cutoff):9.1f} "
              f"{edge_energy_db(end, rate, args.cutoff):8.1f}{cols}")


if __name__ == "__main__":
    main()
