"""Plot the magnitude response of effects from their impulse responses (L5.4): why a phaser isn't a flanger.

    python tools/analyze/plot_fx_response.py renders/l5.4/l5.4_flanger_ir.wav renders/l5.4/l5.4_phaser_ir.wav \
        --out renders/l5.4/flanger_vs_phaser.png

Each WAV is an effect's response to a single impulse (left channel), with its LFO frozen. Its FFT is the
effect's frequency response. Two panels, the same curves: a LINEAR frequency axis, on which a flanger's comb
teeth are evenly spaced (they're at k/τ), and a LOG axis, on which the phaser's few notches are evenly spaced
by ratio. Also prints the notch frequencies (local minima below -20 dB).
"""
from __future__ import annotations

import argparse

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

import wavio


def response(path: str, n_fft: int = 65536) -> tuple[np.ndarray, np.ndarray]:
    rate, x = wavio.load_wav(path, 0)
    X = np.fft.rfft(x, n_fft)
    f = np.fft.rfftfreq(n_fft, 1 / rate)
    return f, 20 * np.log10(np.maximum(np.abs(X), 1e-9))


def notches(f: np.ndarray, db: np.ndarray, below: float = -20.0) -> list[float]:
    i = np.where((db[1:-1] < db[:-2]) & (db[1:-1] < db[2:]) & (db[1:-1] < below))[0] + 1
    return [float(f[k]) for k in i]


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", nargs="+")
    p.add_argument("--out", default="renders/fx_response.png")
    p.add_argument("--max-hz", type=float, default=8000.0, help="upper limit of the linear panel")
    args = p.parse_args()

    fig, (lin, log) = plt.subplots(2, 1, figsize=(10, 7))
    for path in args.wav:
        f, db = response(path)
        name = path.replace("\\", "/").split("/")[-1].removesuffix(".wav")
        ns = notches(f, db)
        shown = ", ".join(f"{v:.0f}" for v in ns[:8]) + (" ..." if len(ns) > 8 else "")
        print(f"{name}: {len(ns)} notches below -20 dB: {shown} Hz")
        lin.plot(f, db, label=name)
        log.semilogx(f[1:], db[1:], label=name)
    lin.set_xlim(0, args.max_hz)
    log.set_xlim(20, 20000)
    for ax, title in ((lin, "linear frequency axis: a comb's teeth are evenly spaced"),
                      (log, "log frequency axis: the phaser's notches are evenly spaced by ratio")):
        ax.set_ylim(-60, 12)
        ax.set_ylabel("dB")
        ax.grid(True, which="both", alpha=0.3)
        ax.set_title(title)
        ax.legend()
    log.set_xlabel("Hz")
    fig.tight_layout()
    fig.savefig(args.out, dpi=110)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
