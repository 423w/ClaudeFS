"""rt60.py: reverb decay (RT60) and echo density from impulse-response WAVs (L5.5).

    python tools/analyze/rt60.py renders/l5.5/l5.5_ir_*.wav --out renders/l5.5/rt60.png
    python tools/analyze/rt60.py renders/l5.5/l5.5_ir_fdn_long_mod0.wav renders/l5.5/l5.5_ir_fdn_long_mod30.wav --roughness

RT60 by Schroeder's backward integration: EDC(t) = sum of h^2 from t to the end, in dB, a line fitted from -5 to
-35 dB (T30), extrapolated to 60 dB. The same method as tools/render/rt60.h (the C++ test uses that one); this one
also plots the EDCs and the normalized echo density over time (Abel & Huang 2006: the fraction of samples beyond
one standard deviation in a 20 ms window, / 0.3173; 1 = as dense as Gaussian noise).

--roughness: how "ringy" a late tail is. The Welch-averaged spectrum of the tail between 1.0 and 2.5 s, in dB,
minus a smooth version of itself (a 1/6-octave moving median); the standard deviation of what's left, from 200 Hz
to 5 kHz. Fixed resonances that persist (modes that ring) make narrow, stable peaks: a larger value.
"""
from __future__ import annotations

import argparse

import matplotlib
import numpy as np
from scipy import signal as sps
from scipy.ndimage import median_filter

import wavio


def edc_db(h: np.ndarray) -> np.ndarray:
    e = np.cumsum((h.astype(np.float64) ** 2)[::-1])[::-1]
    return 10.0 * np.log10(np.maximum(e / e[0], 1e-30))


def rt60(h: np.ndarray, rate: int, hi: float = -5.0, lo: float = -35.0) -> float:
    d = edc_db(h)
    idx = np.where((d <= hi) & (d >= lo))[0]
    if idx.size < 10:
        return float("nan")
    slope = np.polyfit(idx / rate, d[idx], 1)[0]
    return -60.0 / slope


def band(h: np.ndarray, rate: int, centre: float) -> np.ndarray:
    sos = sps.butter(2, [centre / np.sqrt(2), centre * np.sqrt(2)], btype="bandpass", fs=rate, output="sos")
    return sps.sosfilt(sos, h)


def echo_density(h: np.ndarray, rate: int, win_ms: float = 20.0, hop_ms: float = 5.0) -> tuple[np.ndarray, np.ndarray]:
    w = int(win_ms * 1e-3 * rate)
    hop = int(hop_ms * 1e-3 * rate)
    t, ned = [], []
    for start in range(0, len(h) - w, hop):
        x = h[start:start + w]
        sigma = np.sqrt(np.mean(x ** 2))
        ned.append(np.mean(np.abs(x) > sigma) / 0.3173105078629141 if sigma > 0 else 0.0)
        t.append((start + w / 2) / rate)
    return np.array(t), np.array(ned)


def roughness(h: np.ndarray, rate: int, t0: float = 1.0, t1: float = 2.5) -> float:
    x = h[int(t0 * rate):int(t1 * rate)]
    f, p = sps.welch(x, fs=rate, nperseg=8192)
    db = 10.0 * np.log10(np.maximum(p, 1e-30))
    # 1/6 octave at 1 kHz is ~120 Hz: with 5.86 Hz bins that's ~21 bins; use a constant width (fine for 200 Hz-5 kHz).
    smooth = median_filter(db, size=21, mode="nearest")
    sel = (f >= 200) & (f <= 5000)
    return float(np.std(db[sel] - smooth[sel]))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav", nargs="+")
    p.add_argument("--out", help="output PNG (EDC and echo density plots)")
    p.add_argument("--roughness", action="store_true", help="print the late-tail spectral roughness")
    a = p.parse_args()
    if a.out:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    for path in a.wav:
        rate, h = wavio.load_wav(path, channel=0)
        bands = {c: rt60(band(h, rate, c), rate) for c in (250, 1000, 4000)}
        line = f"{path}: RT60 {rt60(h, rate):.2f} s; 250 Hz {bands[250]:.2f}, 1 kHz {bands[1000]:.2f}, 4 kHz {bands[4000]:.2f} s"
        if a.roughness:
            line += f"; late-tail roughness {roughness(h, rate):.2f} dB"
        print(line)
        d = edc_db(h)
        ax1.plot(np.arange(len(d)) / rate, d, label=path.split("/")[-1].split("\\")[-1])
        t, ned = echo_density(h, rate)
        ax2.plot(t * 1000.0, ned, label=path.split("/")[-1].split("\\")[-1])
    ax1.set(xlabel="time (s)", ylabel="energy decay curve (dB)", ylim=(-80, 2), title="Schroeder backward integration")
    ax1.axhline(-5, color="grey", lw=0.5)
    ax1.axhline(-35, color="grey", lw=0.5)
    ax1.legend()
    ax2.set(xlabel="time (ms)", ylabel="normalized echo density", xlim=(0, 300), ylim=(0, 1.3), title="echo density (1 = Gaussian)")
    ax2.legend()
    fig.tight_layout()
    if a.out:
        fig.savefig(a.out, dpi=110)
        print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
