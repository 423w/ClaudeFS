# Real-time rules (CFS)

The checklist for any code reachable from `Engine::process()` or a host's audio callback. Written in
L2.2 from `[BENCINA]` and `[RT101]`, and from what the L2.1 measurements showed. The audio thread has a
**deadline every period** (5.3 ms at 256 frames / 48 kHz, 1.3 ms at 64), and "fast on average" is useless:
one slow call in ten thousand is a click every minute.

## Never, on the audio thread
| Don't | Because | Instead |
|---|---|---|
| allocate or free (`new`, `delete`, `malloc`, growing a `vector`, `std::string`, `std::function` with captures, `make_shared`, a `shared_ptr` dropping the last ref) | the allocator can take a lock or ask the OS for pages: unbounded time | allocate in `prepare()`; fixed-size `std::array`; preallocated pools |
| lock a mutex, wait on a condition variable, join a thread | the lock holder may be a low-priority thread that isn't running (priority inversion) | atomics; lock-free SPSC queues (L2.3) |
| file, console, network I/O, `printf`, logging | the OS can block for milliseconds or more | push log records to a queue; the main thread prints them |
| `Sleep`, `sleep_for`, yield | Windows sleeps ≥ ~15 ms whatever you ask (measured: 12 ms requested → 20–35 ms) | nothing: the callback never waits |
| throw exceptions | unwinding may allocate and takes unbounded time | `noexcept` everywhere; report errors with return values or flags |
| call OS / driver APIs that might block (window messages, COM, registry) | unknown and unbounded | do it on the main thread, hand results over |
| unbounded loops (wait-until-free, retry-forever) | no worst case = no guarantee | bounded loops: process at most N events per block, drop with a counter |

## Also watch for
- **Denormals**: tiny floats in decaying filters/feedback can be 100× slower (L2.6: flush-to-zero).
- **First-touch page faults**: memory allocated in `prepare()` but never written may fault on first use; touch it in `prepare()`.
- **Hidden allocations in the standard library**: `std::sort` (no), `std::stable_sort` (yes, a buffer),
  `std::function` (maybe), `std::string` > ~15 chars (yes), `std::vector::clear()` (no; `shrink_to_fit` yes).
- **Atomics that aren't lock-free**: `static_assert(std::atomic<T>::is_always_lock_free)`. A non-lock-free atomic hides a mutex.
- **Freeing from the audio thread by accident**: e.g. replacing a `std::vector` or a `unique_ptr` held by the engine.
  Ownership changes go to the main thread (swap pointers via a queue; free there).
- **Data shared with the main thread**: only through atomics or SPSC queues; nothing else is safe to read while it's being written.

## How CFS enforces it
- `fss::util::RealtimeGuard` marks real-time scopes (`Engine::process`, the standalone's `render`).
- `fss_rt_trap` (an object library) replaces global `operator new`/`delete`; inside a guard it counts or
  aborts. Linked into `fss_tests` (counting) and Debug `fss_standalone` (abort on the spot).
- `tests/test_realtime.cpp`: 2000 blocks of random notes, steals, bends, velocity-0 note-offs and panics through
  `Engine::process` must produce **0** violations.
- Code review: every new primitive's `tick()`/`process()` is `noexcept` and touches only preallocated members.

## Checklist for a new primitive or engine feature
1. Does `tick()`/`process()` allocate, free, lock, print, sleep, throw, or call the OS? (All must be "no".)
2. Are all buffers sized in `prepare()` (or fixed arrays)?
3. Is every loop bounded by something the host controls (block size, voice count, event count)?
4. Is anything shared with another thread? Then it's an atomic or goes through an SPSC queue.
5. Does the guarded test still report 0 violations with the feature exercised?
