// fss/util/denormal_guard.h: flush denormals to zero for the lifetime of a scope (L2.6).
//
// DENORMALS. A float normally stores 24 bits of precision and an 8-bit exponent down to 2^−126 ≈ 1.2e−38.
// Below that, IEEE 754 keeps going with "denormal" (subnormal) numbers down to 1.4e−45 by giving up precision
// bits instead. That's gradual underflow, and it's mathematically lovely, but many x86 CPUs handle
// denormal operands or results in a slow microcode path, tens to hundreds of cycles per operation.
// Audio produces them all the time: every decaying tail (a filter after its input stops, a reverb, a
// smoother gliding to 0) passes through 1e−38 on its way to silence, and a resonant filter's states can sit
// there for a long time. Nobody can hear a 1e−40 signal, but the CPU spike is real.
//
// THE FIX. SSE's control register MXCSR has two bits for exactly this:
//   FTZ (flush to zero, bit 15):     results that would be denormal become 0
//   DAZ (denormals are zero, bit 6): denormal *inputs* are read as 0
// Setting both at the start of process() makes the slow path unreachable. MXCSR belongs to the *thread*, and
// the audio thread belongs to the host, so this is RAII: save the host's MXCSR, set ours, restore theirs on
// exit (CLAP and VST hosts usually set FTZ/DAZ themselves; a polite plugin still restores what it found).
// [RT-RULES: no state leaks out of process()]
//
// Portability: x86/x64 only (SSE). On ARM (Apple Silicon) the equivalent is the FZ bit of FPCR; this guard
// compiles to nothing there for now. <immintrin.h> is a compiler intrinsic header, not a library: no OS
// calls, no I/O, no framework types, so the core's "standard library only" rule still holds in spirit.
#pragma once

#include <cstdint>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define FSS_HAS_MXCSR 1
#else
#define FSS_HAS_MXCSR 0
#endif

namespace fss::util {

class DenormalGuard {
public:
#if FSS_HAS_MXCSR
    static constexpr std::uint32_t kFtz = 0x8000; // bit 15: flush-to-zero
    static constexpr std::uint32_t kDaz = 0x0040; // bit 6: denormals-are-zero

    DenormalGuard() noexcept : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | kFtz | kDaz); }
    ~DenormalGuard() { _mm_setcsr(saved_); }
#else
    DenormalGuard() noexcept = default;
#endif
    DenormalGuard(const DenormalGuard&) = delete;
    DenormalGuard& operator=(const DenormalGuard&) = delete;

    /// True if this build can actually flush denormals (x86/x64).
    static constexpr bool supported() noexcept { return FSS_HAS_MXCSR != 0; }

private:
#if FSS_HAS_MXCSR
    std::uint32_t saved_;
#endif
};

} // namespace fss::util
