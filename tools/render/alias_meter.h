// alias_meter.h: measure how much of a periodic signal is NOT at its harmonics (L1.8).
// Host-side analysis (shared by fss_render's l1.8 scenario and tests/test_waveforms.cpp), not core DSP.
//
// The trick that makes this exact, with no window and no leakage:
//   Choose f0 = k·fs/N with k an integer. Then the oscillator's phase increment is k/N cycles per sample,
//   so after N samples the phase has advanced exactly k cycles: the signal repeats every N samples. A
//   length-N DFT of a signal that is periodic in N has no leakage at all: every component sits exactly on
//   a bin. The harmonics h·f0 sit on bins h·k (folded around N/2 if needed), and EVERYTHING else (aliases,
//   rounding noise) sits on other bins.
//   (With N = 2^m and k an integer, the uint32 phase increment k·2^(32−m) is exact, so this holds
//   bit for bit, not just approximately.)
//
// THE TRAP: pick k ODD (coprime with N = 2^m). If f0 divided fs exactly (say f0 = 1000 Hz at 48 kHz),
// the signal would repeat every P = 48 samples, and a folded harmonic lands on |h·f0 − m·fs| = |h − 48m|·f0:
// a multiple of f0, i.e. ON a harmonic. The aliases would hide inside the harmonics (changing their
// levels a little) and this meter would read "no aliasing". With k coprime to N, an alias of harmonic h
// lands on bin (h·k mod N), which is a harmonic bin j·k only if k divides the fold count, i.e. only for
// h ≥ N: harmonics thousands of times quieter than the fundamental. Negligible.
//
// Energy bookkeeping (Parseval): mean power = (1/N)·Σ x[n]² = Σ over bins of |X[b]|²/N².
//   DC power:        (X[0]/N)²
//   harmonic power:  2·|X[b]|²/N² for each harmonic bin b in (0, N/2) (×2: the mirror bin N − b)
//   alias power:     total − DC − harmonics
// We only compute DFT bins at the harmonics (direct sums, O(N) each), so no FFT library is needed.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

namespace fss::render {

struct AliasMeasurement {
    double totalPower = 0.0;    // mean square, DC included
    double dcPower = 0.0;
    double harmonicPower = 0.0; // harmonics strictly below Nyquist
    double aliasPower = 0.0;    // everything else: aliases + rounding noise
    double lowAliasPower = 0.0; // the part of aliasPower in bins below `lowBins` (0 if not requested)

    /// Alias power relative to the total (DC excluded), in dB. −∞ is clamped to −300.
    [[nodiscard]] double aliasDb() const { return toDb(aliasPower); }
    /// Same, counting only aliases below the cutoff given to measureAliasing.
    [[nodiscard]] double lowAliasDb() const { return toDb(lowAliasPower); }

private:
    [[nodiscard]] double toDb(double p) const
    {
        const double ac = totalPower - dcPower;
        return 10.0 * std::log10(std::max(p, 1e-30 * ac) / ac);
    }
};

/// `x` must hold exactly N samples of a signal with f0 = k·fs/N (k odd, N a power of two).
/// lowBins > 0 also measures the alias power in bins 1 .. lowBins−1 (bin b = b·fs/N Hz), i.e. "how much
/// aliasing lands where hearing is sharp", since most of a naive or PolyBLEP oscillator's alias energy sits
/// just below Nyquist, where it's hard to hear (see the L1.8 journal). Costs N multiply-adds per bin.
inline AliasMeasurement measureAliasing(std::span<const float> x, std::size_t k, std::size_t lowBins = 0)
{
    const std::size_t n = x.size();
    // cos/sin table for one cycle: bin b at sample i needs angle 2π·(b·i mod N)/N. Indexing a table with
    // an exact integer product avoids accumulating phase error over N samples.
    std::vector<double> c(n), s(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n);
        c[i] = std::cos(a);
        s[i] = std::sin(a);
    }

    AliasMeasurement m;
    double sum = 0.0;
    for (float v : x) {
        m.totalPower += static_cast<double>(v) * v;
        sum += v;
    }
    const double nd = static_cast<double>(n);
    m.totalPower /= nd;
    m.dcPower = (sum / nd) * (sum / nd);

    // Power of bin b (0 < b < N/2), both mirror halves: 2·|X[b]|²/N².
    const auto binPower = [&](std::size_t b) {
        double re = 0.0, im = 0.0;
        std::size_t idx = 0; // (b·i) mod N, stepped incrementally
        for (std::size_t i = 0; i < n; ++i) {
            re += x[i] * c[idx];
            im -= x[i] * s[idx];
            idx += b;
            if (idx >= n)
                idx -= n;
        }
        return 2.0 * (re * re + im * im) / (nd * nd);
    };

    for (std::size_t b = k; 2 * b < n; b += k) // harmonic bins below Nyquist: k, 2k, 3k, ...
        m.harmonicPower += binPower(b);
    m.aliasPower = m.totalPower - m.dcPower - m.harmonicPower;

    for (std::size_t b = 1; b < lowBins && 2 * b < n; ++b)
        if (b % k != 0) // not a harmonic
            m.lowAliasPower += binPower(b);
    return m;
}

} // namespace fss::render
