"""Write known test signals to renders/ so the analysis scripts can be checked against known answers.

    python tools/analyze/make_test_signals.py

  sine_1k_-6dB.wav     1 kHz sine, amplitude 0.5. plot_spectrum must report a peak at 1000 Hz, −6.02 dBFS.
  naive_saw_5k.wav     5 kHz sawtooth computed the naive way (no anti-aliasing). Partials above 24 kHz fold
                       back below Nyquist at *inharmonic* frequencies (L0.2's [THINK] #3).
  naive_saw_sweep.wav  naive saw sweeping 100 Hz → 12 kHz. In the spectrogram, aliases are the lines that
                       go *down* while the note goes up.

These come from numpy, not from fss_core. CFS's own oscillators start in L1.2, its WAV writer in L1.1.
"""
from __future__ import annotations

import numpy as np
from scipy.io import wavfile

import wavio

RATE = 48_000


def naive_saw(phase_cycles: np.ndarray) -> np.ndarray:
    """Sawtooth from a phase in cycles: ramps −1 → +1 each cycle, with an instant jump back.

    The instant jump is the problem: it contains energy at *every* harmonic, far beyond Nyquist, and
    sampling folds all of it back into the audible band. PolyBLEP (L1.8) rounds off exactly that jump.
    """
    return 2.0 * (phase_cycles % 1.0) - 1.0


def write(name: str, x: np.ndarray) -> None:
    wavio.RENDERS.mkdir(exist_ok=True)
    path = wavio.RENDERS / name
    wavfile.write(path, RATE, x.astype(np.float32))  # 32-bit float WAV: no quantization noise floor
    print(f"wrote {path}")


def main() -> None:
    n = np.arange(RATE)  # one second
    t = n / RATE

    write("sine_1k_-6dB.wav", 0.5 * np.sin(2 * np.pi * 1000.0 * t))
    # Phase computed with *integers*: (5000·n mod 48000) / 48000 is exact. The obvious float version,
    # (5000.0 * t) % 1.0, puts 56 of the 1000 wrap points on the wrong side of the jump, because 5000·n/48000
    # comes out as 4.9999999… instead of 5. Each one flips a sample from −0.5 to +0.5, which adds a
    # noise floor at −55…−80 dB (seen while writing L0.2). Phase precision is decision D5 in L1.2.
    write("naive_saw_5k.wav", 0.5 * naive_saw((5000 * n % RATE) / RATE))

    # Exponential sweep: frequency rises by a constant ratio per second, which is how pitch works.
    # Phase is the running sum of frequency/rate. That's the phase accumulator of L1.2, in numpy.
    seconds = 4
    m = np.arange(RATE * seconds)
    freq = 100.0 * (12_000.0 / 100.0) ** (m / len(m))
    phase = np.cumsum(freq / RATE)
    write("naive_saw_sweep.wav", 0.5 * naive_saw(phase))


if __name__ == "__main__":
    main()
