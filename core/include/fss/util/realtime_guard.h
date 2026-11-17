// fss/util/realtime_guard.h: mark "this code runs on the audio thread" so violations can be caught (L2.2).
//
//   void Engine::process(...) noexcept {
//       const util::RealtimeGuard rt;   // from here to the end of the scope we're "real-time"
//       ...
//   }
//
// The guard itself does almost nothing: it bumps a thread_local depth counter. The *trap*
// (core/src/rt_alloc_trap.cpp, linked into tests and Debug hosts) replaces the global operator new and
// delete and, whenever the current thread is inside a guard, records a violation (or aborts, in Debug
// hosts). So "process() never allocates" stops being a promise and becomes something a test checks.
//
// Why thread_local: the same code (an Engine) may run on the audio thread in one host and on the main
// thread in another (the offline renderer). Only the *current thread's* state says whether this call is
// real-time. Cost: one increment and one decrement of a thread-local int per guarded call.
//
// See docs/RT-RULES.md for the full checklist this enforces one rule of.
#pragma once

#include <cstdint>

namespace fss::util {

namespace detail {
inline thread_local int realtimeDepth = 0;
} // namespace detail

/// True while the calling thread is inside at least one RealtimeGuard.
[[nodiscard]] inline bool inRealtimeScope() noexcept { return detail::realtimeDepth > 0; }

class RealtimeGuard {
public:
    RealtimeGuard() noexcept { ++detail::realtimeDepth; }
    ~RealtimeGuard() { --detail::realtimeDepth; }
    RealtimeGuard(const RealtimeGuard&) = delete;
    RealtimeGuard& operator=(const RealtimeGuard&) = delete;
};

/// Temporarily lift the guard, for code that is *allowed* to allocate even though it's called from a
/// real-time scope (there should be almost none; each use deserves a comment saying why).
class RealtimeExemption {
public:
    RealtimeExemption() noexcept : saved_(detail::realtimeDepth) { detail::realtimeDepth = 0; }
    ~RealtimeExemption() { detail::realtimeDepth = saved_; }
    RealtimeExemption(const RealtimeExemption&) = delete;
    RealtimeExemption& operator=(const RealtimeExemption&) = delete;

private:
    int saved_;
};

// ---- the allocation trap's interface (implemented in rt_alloc_trap.cpp, if it's linked) ----------

enum class RealtimeTrapMode : std::uint8_t {
    Count, // record the violation and carry on (tests check the counter)
    Abort, // stop the program right there, so a debugger lands on the offending allocation (Debug hosts)
};

/// Number of allocations/frees seen inside real-time scopes since the program started (or the last reset).
/// Always 0 if the trap isn't linked into this program.
[[nodiscard]] std::uint64_t realtimeViolations() noexcept;
void resetRealtimeViolations() noexcept;
void setRealtimeTrapMode(RealtimeTrapMode mode) noexcept;
/// True if this program has the trap linked in (so a 0 count actually means something).
[[nodiscard]] bool realtimeTrapInstalled() noexcept;

} // namespace fss::util
