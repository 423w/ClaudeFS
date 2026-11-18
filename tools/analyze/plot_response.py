"""Plot a filter's frequency response: MEASURED from an impulse-response WAV, optionally over the COMPUTED one.
    python tools/analyze/plot_response.py renders/l1.9_ir_lowpass.wav
    python tools/analyze/plot_response.py renders/l1.9_ir_lowpass.wav --computed renders/l1.9_ir_lowpass.csv
    python tools/analyze/plot_response.py renders/l1.9_ir_*.wav --computed-same-name     # one figure per file

Why an impulse? A linear filter is completely described by its impulse response h[n], and the spectrum of
h[n] IS the frequency response H(e^jw): no window and no level scaling (unlike plot_spectrum.py, which is for
signals). The FFT is taken with heavy zero padding so the curve is smooth, and the IR must have decayed to
nothing inside the file (otherwise the truncation shows up as ripple).

The computed CSV (hz, db[, phase_deg]) is what fss_render writes from responseAt(); the two curves should
lie on top of each other (tests/test_filters.cpp holds them to 0.1 dB).
"""
import argparse
import glob
import os

import numpy as np

from wavio import finish, load_wav, output_path


def measured_response(path):
    rate, h = load_wav(path, channel=0)
    n = 1 << int(np.ceil(np.log2(max(len(h), 1) * 8)))  # zero-pad x8 for a smooth curve
    H = np.fft.rfft(h, n)
    f = np.fft.rfftfreq(n, 1 / rate)
    return rate, f, 20 * np.log10(np.abs(H) + 1e-12)


def plot_one(wav, csv, out, show):
    import matplotlib
    if not show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rate, f, db = measured_response(wav)
    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.semilogx(f[1:], db[1:], label="measured (FFT of impulse response)", lw=2.5, alpha=0.6)
    if csv:
        c = np.genfromtxt(csv, delimiter=",", names=True)
        ax.semilogx(c["hz"], c["db"], "k--", lw=1, label="computed (responseAt)")
        # Worst disagreement where the response is above -60 dB (below that, dB differences are meaningless).
        m = np.interp(c["hz"], f, db)
        ok = c["db"] > -60
        if ok.any():
            print(f"{os.path.basename(wav)}: max |measured - computed| = {np.max(np.abs(m[ok] - c['db'][ok])):.4f} dB "
                  f"(where computed > -60 dB)")
    ax.set_xlim(10, rate / 2)
    ax.set_ylim(max(np.min(db[1:]) - 3, -90), np.max(db) + 3)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("gain (dB)")
    ax.set_title(os.path.basename(wav))
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    finish(fig, output_path(wav, "response", out), show)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", nargs="+", help="impulse-response WAV(s); wildcards allowed")
    p.add_argument("--computed", help="CSV of the computed response (hz,db,...) for a single WAV")
    p.add_argument("--computed-same-name", action="store_true", help="use <wav>.csv next to each WAV")
    p.add_argument("--out", help="output PNG (single WAV only)")
    p.add_argument("--show", action="store_true")
    args = p.parse_args()
    wavs = [w for pattern in args.wav for w in (glob.glob(pattern) or [pattern])]
    for w in wavs:
        csv = args.computed if len(wavs) == 1 else None
        if args.computed_same_name:
            csv = os.path.splitext(w)[0] + ".csv"
        plot_one(w, csv, args.out if len(wavs) == 1 else None, args.show)


if __name__ == "__main__":
    main()
