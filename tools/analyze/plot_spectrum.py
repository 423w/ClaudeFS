"""Plot the magnitude spectrum of a WAV (or part of it) in dBFS on a log-frequency axis.

    python tools/analyze/plot_spectrum.py renders/foo.wav
    python tools/analyze/plot_spectrum.py renders/foo.wav --start 0.2 --duration 0.5 --peaks 12
    python tools/analyze/plot_spectrum.py renders/foo.wav --window rect      # see why windows matter

How it works:
  1. Take N samples and multiply them by a *window* (Hann by default).
  2. FFT. Bin k is the frequency k·fs/N, with resolution fs/N Hz between bins.
  3. Scale so that a full-scale sine reads 0 dBFS, then plot 20·log10(|X|).

Why the window matters: the FFT assumes the N samples repeat forever. Unless the signal happens to fit a
whole number of cycles in N, the jump where the end wraps round to the start acts like a click, and its
energy smears across every bin ("spectral leakage"). A rectangular window (no window) leaks badly: skirts
only fall ~6 dB/octave, so a loud partial buries quiet neighbours. Hann tapers both ends to zero, so the
wrap-around has no jump: skirts fall ~18 dB/octave, at the price of a main lobe two bins wider.
Blackman goes further (lower skirts, wider lobe). [JOS-SASP] covers this properly; L3.6 goes deeper.

Calibration: dividing by sum(window)/2 makes a sine of amplitude A peak at 20·log10(A) dBFS when it sits
exactly on a bin (a 0.5-amplitude sine reads −6.02 dB). Off-bin, a Hann peak reads up to ~1.4 dB low
(scalloping loss). --peaks fixes that by fitting a parabola through the three dB values around each peak.
"""
from __future__ import annotations

import argparse

import matplotlib
import numpy as np
from scipy import signal as sps

import wavio

WINDOWS = {"hann": "hann", "blackman": "blackman", "rect": "boxcar"}


def spectrum_db(x: np.ndarray, rate: int, window: str = "hann") -> tuple[np.ndarray, np.ndarray]:
    """Return (freqs_hz, magnitude_dbfs) of x, calibrated so a full-scale on-bin sine reads 0 dB."""
    w = sps.get_window(WINDOWS[window], len(x), fftbins=True)
    X = np.fft.rfft(x * w)
    # A sine of amplitude A shows up as A/2 · sum(w) in its bin (half the energy is in the negative-frequency
    # mirror that rfft drops). Dividing by sum(w)/2 makes the peak read A.
    mag = np.abs(X) / (np.sum(w) / 2.0)
    freqs = np.fft.rfftfreq(len(x), 1.0 / rate)
    return freqs, 20.0 * np.log10(np.maximum(mag, 1e-12))  # floor at −240 dB so log10(0) can't happen


def find_peaks(freqs: np.ndarray, db: np.ndarray, count: int, floor_db: float) -> list[tuple[float, float]]:
    """The `count` loudest local maxima above floor_db, refined by parabolic interpolation.

    Fitting a parabola through the 3 dB values around a peak bin estimates where the true peak sits
    *between* bins. With a Hann window that's good to a small fraction of a bin and a few tenths of a dB.
    (Measured: 10.5 cycles of a −6.02 dB sine reads −5.70 dB with Hann; with rect it's off by 2.7 dB and 4 Hz.)
    """
    idx, _ = sps.find_peaks(db, height=floor_db)
    idx = idx[np.argsort(db[idx])[::-1][:count]]
    out = []
    bin_hz = freqs[1] - freqs[0]
    for i in idx:
        if 0 < i < len(db) - 1:
            a, b, c = db[i - 1], db[i], db[i + 1]
            d = 0.5 * (a - c) / (a - 2 * b + c)  # offset of the vertex from bin i, in bins (−0.5..0.5)
            out.append((freqs[i] + d * bin_hz, b - 0.25 * (a - c) * d))
        else:
            out.append((freqs[i], db[i]))
    return sorted(out)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav")
    p.add_argument("--start", type=float, default=0.0, help="seconds")
    p.add_argument("--duration", type=float, default=None, help="seconds (default: whole file)")
    p.add_argument("--channel", default="mix", help='"mix" or a channel index (0 = left)')
    p.add_argument("--window", choices=WINDOWS, default="hann")
    p.add_argument("--fmin", type=float, default=20.0, help="lowest frequency shown (Hz)")
    p.add_argument("--floor", type=float, default=-120.0, help="lowest dB shown")
    p.add_argument("--linear", action="store_true", help="linear frequency axis (good for aliasing)")
    p.add_argument("--peaks", type=int, default=0, help="print and mark the N loudest peaks")
    p.add_argument("--out", help="output PNG (default renders/<name>.spectrum.png)")
    p.add_argument("--show", action="store_true", help="also open a window")
    args = p.parse_args()

    if not args.show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    channel = args.channel if args.channel == "mix" else int(args.channel)
    rate, x = wavio.load_wav(args.wav, channel)
    x = wavio.slice_seconds(x, rate, args.start, args.duration)
    freqs, db = spectrum_db(x, rate, args.window)

    fig, ax = plt.subplots(figsize=(11, 5))
    ax.plot(freqs[1:], db[1:], lw=0.7)  # skip DC (0 Hz): it has no place on a log axis
    if not args.linear:
        ax.set_xscale("log")
    ax.set_xlim(0 if args.linear else args.fmin, rate / 2)
    ax.set_ylim(args.floor, max(6.0, float(db.max()) + 6.0))
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("magnitude (dBFS)")
    ax.set_title(f"{args.wav}: {len(x)} samples @ {rate} Hz, {args.window} window, "
                 f"{rate / len(x):.2f} Hz/bin")
    ax.grid(alpha=0.3, which="both")

    if args.peaks:
        peaks = find_peaks(freqs, db, args.peaks, args.floor)
        print(f"{'freq (Hz)':>12} {'level (dBFS)':>13}")
        for f, level in peaks:
            print(f"{f:12.2f} {level:13.2f}")
            ax.plot([f], [level], "rv", ms=5)
    wavio.finish(fig, wavio.output_path(args.wav, "spectrum", args.out), args.show)


if __name__ == "__main__":
    main()
