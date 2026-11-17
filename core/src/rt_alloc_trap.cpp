// rt_alloc_trap.cpp: replaces the global operator new/delete to catch allocations on the audio thread (L2.2).
//
// HOW IT'S LINKED: as a CMake OBJECT library (fss_rt_trap), added directly to the executables that want
// it (the tests, and Debug builds of real-time hosts). Not part of the fss_core static library on
// purpose: from a .lib, the linker only pulls in an object file if something references one of its symbols,
// and operator new is already satisfied by the C runtime, so the replacement would silently never be
// used. Object files passed directly to the linker always are.
//
// Replacing operator new is a standard, supported C++ mechanism: the program may define these signatures
// once, and every `new`, std::vector growth, std::string, std::function... goes through them.
//
// The fallback stubs for programs WITHOUT the trap live in realtime_guard_stubs.cpp (in fss_core).
#include <fss/util/realtime_guard.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::uint64_t> gViolations{0};
std::atomic<fss::util::RealtimeTrapMode> gMode{fss::util::RealtimeTrapMode::Count};

void check() noexcept
{
    if (!fss::util::inRealtimeScope())
        return;
    gViolations.fetch_add(1, std::memory_order_relaxed);
    if (gMode.load(std::memory_order_relaxed) == fss::util::RealtimeTrapMode::Abort) {
        // We're about to die anyway, so the usual "no I/O on the audio thread" rule no longer matters.
        std::fputs("fss: allocation or free on the real-time thread (RealtimeGuard) - aborting\n", stderr);
        std::abort(); // a debugger stops here with the offending call on the stack
    }
}

void* allocate(std::size_t size)
{
    check();
    // malloc(0) may return nullptr; operator new must return a unique non-null pointer.
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* allocateAligned(std::size_t size, std::align_val_t align)
{
    check();
    // MSVC's malloc can't do over-aligned allocations; _aligned_malloc can (and needs _aligned_free).
#ifdef _MSC_VER
    if (void* p = _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(align)))
        return p;
#else
    if (void* p = std::aligned_alloc(static_cast<std::size_t>(align),
                                     ((size == 0 ? 1 : size) + static_cast<std::size_t>(align) - 1) &
                                         ~(static_cast<std::size_t>(align) - 1)))
        return p;
#endif
    throw std::bad_alloc();
}

void freeAligned(void* p) noexcept
{
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}

} // namespace

// ---- the replaceable global allocation functions ------------------------------------------------
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t a) { return allocateAligned(size, a); }
void* operator new[](std::size_t size, std::align_val_t a) { return allocateAligned(size, a); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size); } catch (...) { return nullptr; }
}

// Freeing on the audio thread is just as bad as allocating (the allocator may take a lock, or return
// memory to the OS), so delete is checked too. Deleting nullptr is a no-op and allowed.
void operator delete(void* p) noexcept { if (p) { check(); std::free(p); } }
void operator delete[](void* p) noexcept { if (p) { check(); std::free(p); } }
void operator delete(void* p, std::size_t) noexcept { if (p) { check(); std::free(p); } }
void operator delete[](void* p, std::size_t) noexcept { if (p) { check(); std::free(p); } }
void operator delete(void* p, std::align_val_t) noexcept { if (p) { check(); freeAligned(p); } }
void operator delete[](void* p, std::align_val_t) noexcept { if (p) { check(); freeAligned(p); } }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { if (p) { check(); freeAligned(p); } }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { if (p) { check(); freeAligned(p); } }

// ---- the trap's control interface (declared in realtime_guard.h) ---------------------------------
namespace fss::util {
std::uint64_t realtimeViolations() noexcept { return gViolations.load(std::memory_order_relaxed); }
void resetRealtimeViolations() noexcept { gViolations.store(0, std::memory_order_relaxed); }
void setRealtimeTrapMode(RealtimeTrapMode mode) noexcept { gMode.store(mode, std::memory_order_relaxed); }
bool realtimeTrapInstalled() noexcept { return true; }
} // namespace fss::util
