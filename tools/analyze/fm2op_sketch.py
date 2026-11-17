"""L0.4 sketch: a 2-operator phase-modulation patch in numpy, standing in for "turn knobs in Dexed".

    python tools/analyze/fm2op_sketch.py

NOT the synth. CFS's real operator is C++ and arrives in L1.4. This is Claude's substitute for L0.4's
hands-on step 1 (Claude can't play Dexed): render the same experiment and *measure* it instead of listening.

The patch (Chowning's simple FM [CHOWNING73], written as phase modulation, which is what DX-style synths do):

    y(t) = sin( 2π·fc·t  +  I · sin(2π·fm·t) )        fm = ratio · fc,  I = modulation index

Theory: y = Σ_k J_k(I) · sin(2π(fc + k·fm)t) for k = …−2, −1, 0, 1, 2…  (J_k = Bessel functions of the
first kind). So: sidebands at fc ± k·fm with amplitudes |J_k(I)|. Sidebands that land below 0 Hz reflect
to positive frequency with their sign flipped, and can add to or cancel a sideband already there.

For each ratio it writes:
  renders/l04_fm_r<ratio>_sweep.wav   index ramps 0 → 8 over 4 s ("sweeping the modulator level")
  and prints the measured partials at a fixed index next to the Bessel prediction.
"""
from __future__ import annotations

import numpy as np
from scipy.io import wavfile
from scipy.special import jv

import plot_spectrum
import wavio

RATE = 48_000
FC = 400.0          # carrier frequency. Low enough that many sidebands fit below Nyquist
RATIOS = [1.0, 2.0, 3.0, 3.5, 1.41]
STATIC_INDEX = 2.0  # for the measured table


def pm(index: np.ndarray | float, ratio: float, seconds: float) -> np.ndarray:
    n = np.arange(int(RATE * seconds))
    # Phases in cycles from integer sample counts (exact; see the wrap-point lesson in L0.2's journal).
    car = FC * n / RATE
    mod = ratio * FC * n / RATE
    return np.sin(2 * np.pi * car + index * np.sin(2 * np.pi * mod))


def predicted(ratio: float, index: float, kmax: int = 12) -> dict[float, float]:
    """Sum the Bessel sidebands, folding negative frequencies, into {freq_hz: amplitude}."""
    parts: dict[float, float] = {}
    for k in range(-kmax, kmax + 1):
        f = FC + k * ratio * FC
        a = jv(k, index)
        if f < 0:          # sin(−x) = −sin(x): reflect, flip sign
            f, a = -f, -a
        if f == 0:         # a *sine* at 0 Hz is sin(0) = 0: that sideband simply vanishes (ratio 1, k = −1).
            continue       # (With cos instead of sin it would be a DC offset. The phase convention matters.)
        key = round(f, 6)
        parts[key] = parts.get(key, 0.0) + a
    return {f: abs(a) for f, a in sorted(parts.items()) if f < RATE / 2 and abs(a) > 1e-4}


def main() -> None:
    wavio.RENDERS.mkdir(exist_ok=True)
    for ratio in RATIOS:
        # 1) The sweep, for listening and for spectrogram.py.
        seconds = 4.0
        idx = np.linspace(0.0, 8.0, int(RATE * seconds))
        sweep = 0.5 * pm(idx, ratio, seconds)
        name = f"l04_fm_r{ratio:g}_sweep.wav"
        wavfile.write(wavio.RENDERS / name, RATE, sweep.astype(np.float32))

        # 2) A static index, measured vs predicted.
        x = pm(STATIC_INDEX, ratio, 1.0)
        freqs, db = plot_spectrum.spectrum_db(x, RATE, "hann")
        pred = predicted(ratio, STATIC_INDEX)
        print(f"\n=== ratio {ratio:g} (fm = {ratio * FC:g} Hz), index {STATIC_INDEX:g}  -> renders/{name}")
        print(f"{'freq Hz':>9} {'x fc':>6} {'predicted dB':>13} {'measured dB':>12}")
        for f, a in pred.items():
            if 20 * np.log10(a) < -60:
                continue
            i = int(np.argmin(np.abs(freqs - f)))
            meas = db[max(i - 1, 0):i + 2].max()   # within ±1 bin (Hann main lobe)
            print(f"{f:9.1f} {f / FC:6.3g} {20 * np.log10(a):13.2f} {meas:12.2f}")


if __name__ == "__main__":
    main()
