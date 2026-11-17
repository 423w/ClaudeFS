// fss/util/units.h: strong unit types for the places where humans type numbers (L1.6).
//
// A `double` called hz and a `float` called cents are the same type to the compiler, so
// `setDetune(noteHz)` compiles and sounds horrible. Wrapping each unit in its own struct with an
// EXPLICIT constructor makes that a compile error:
//
//     OperatorParams p;  p.detune = Hz{440};      // error: no conversion from Hz to Cents
//     p.detune = 7.0f;                             // error: constructor is explicit
//     p.detune = 7.0_cents;                        // fine
//
// Where they're used (and where they're NOT): at the patch / note boundary (Patch, NoteContext), where
// values come from people and presets and mixing units is the real risk. The DSP primitives (Phasor,
// Adsr) keep plain doubles with the unit in the name (`setFrequency(double hz)`): they're called from a
// few lines of trusted code per lesson, and wrapping every inner-loop number would be the "annoying" end
// of the trade-off ([THINK] in L1.6). Zero cost either way: each struct is one number, all constexpr.
#pragma once

#include <cmath>

namespace fss::util {

struct Hz {
    double value = 0.0;
    constexpr Hz() = default;
    explicit constexpr Hz(double v) : value(v) {}
};

/// A frequency ratio (dimensionless, > 0): 2 = an octave up, 1.5 ≈ a fifth.
struct Ratio {
    double value = 1.0;
    constexpr Ratio() = default;
    explicit constexpr Ratio(double v) : value(v) {}
};

struct Cents {
    double value = 0.0;
    constexpr Cents() = default;
    explicit constexpr Cents(double v) : value(v) {}
};

struct Semitones {
    double value = 0.0;
    constexpr Semitones() = default;
    explicit constexpr Semitones(double v) : value(v) {}
};

/// Only the operations that mean something physically are defined:
constexpr Hz operator*(Hz f, Ratio r) { return Hz{f.value * r.value}; }    // a frequency times a ratio is a frequency
constexpr Ratio operator*(Ratio a, Ratio b) { return Ratio{a.value * b.value}; }
constexpr Cents operator+(Cents a, Cents b) { return Cents{a.value + b.value}; }
constexpr Semitones operator+(Semitones a, Semitones b) { return Semitones{a.value + b.value}; }
// Unary minus: needed for negative literals (-12.0_st parses as -(12.0_st)). Forgetting these was the
// first thing the strong types "caught" in L1.6: a reminder that every operator must be added by hand.
constexpr Cents operator-(Cents c) { return Cents{-c.value}; }
constexpr Semitones operator-(Semitones s) { return Semitones{-s.value}; }
// Hz + Hz, Hz * Hz, Cents * Ratio, ... are deliberately missing.

[[nodiscard]] inline Ratio toRatio(Cents c) { return Ratio{std::exp2(c.value / 1200.0)}; }
[[nodiscard]] inline Ratio toRatio(Semitones s) { return Ratio{std::exp2(s.value / 12.0)}; }

namespace literals {
constexpr Hz operator""_Hz(long double v) { return Hz{static_cast<double>(v)}; }
constexpr Hz operator""_Hz(unsigned long long v) { return Hz{static_cast<double>(v)}; }
constexpr Cents operator""_cents(long double v) { return Cents{static_cast<double>(v)}; }
constexpr Cents operator""_cents(unsigned long long v) { return Cents{static_cast<double>(v)}; }
constexpr Semitones operator""_st(long double v) { return Semitones{static_cast<double>(v)}; }
constexpr Semitones operator""_st(unsigned long long v) { return Semitones{static_cast<double>(v)}; }
} // namespace literals

} // namespace fss::util
