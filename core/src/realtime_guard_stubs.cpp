// realtime_guard_stubs.cpp: the trap's control functions for programs that DON'T link the allocation trap.
//
// This file must stay on its own (nothing else in it): the linker pulls an object file out of the
// fss_core static library only to resolve a symbol nobody else defined. When rt_alloc_trap.cpp is linked
// into the program directly, it defines these functions first, so this object is never pulled in and
// there's no duplicate. When the trap isn't linked, these stubs report "no trap, no violations".
#include <fss/util/realtime_guard.h>

namespace fss::util {
std::uint64_t realtimeViolations() noexcept { return 0; }
void resetRealtimeViolations() noexcept {}
void setRealtimeTrapMode(RealtimeTrapMode) noexcept {}
bool realtimeTrapInstalled() noexcept { return false; }
} // namespace fss::util
