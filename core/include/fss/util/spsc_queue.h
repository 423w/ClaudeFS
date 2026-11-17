// fss/util/spsc_queue.h: a bounded single-producer single-consumer (SPSC) lock-free ring buffer (L2.3).
//
// The problem it solves (ARCHITECTURE.md §4): MIDI arrives on a WinMM system thread, parameter changes on
// the UI thread, but only the audio thread may touch the engine. The audio thread must never wait for
// another thread (a mutex held by a descheduled UI thread can cost milliseconds = an audible dropout),
// so the hand-over goes through a queue that needs no lock at all.
//
// Why SPSC is so much easier than MPMC: with exactly one producer and one consumer, each index has exactly
// ONE writer. The producer is the only thread that writes `head_` (the next slot to fill), the consumer is
// the only one that writes `tail_` (the next slot to read). Nobody ever races to write the same variable,
// so we need no compare-and-swap loops, only loads and stores with the right memory ordering.
// Consequence: push() and pop() each finish in a fixed, small number of steps whatever the other thread
// does. That makes the queue not just lock-free but *wait-free*, which is what real-time code wants.
//
// Indices: `head_` and `tail_` count up forever (they are never wrapped to Capacity). The slot is
// `index & (Capacity - 1)`, a mask instead of `%`, which is why Capacity must be a power of two.
//   size  = head - tail          (unsigned subtraction, correct even after size_t overflow, because
//                                 Capacity divides 2^64, so the mask stays consistent across the wrap)
//   empty = head == tail
//   full  = head - tail == Capacity
// Monotonic counters let us use ALL Capacity slots. The textbook alternative (wrapped indices) can't tell
// "full" from "empty" when head == tail, so it wastes one slot to keep them apart.
//
// Memory ordering, the heart of it (read [PRESHING] "Acquire and Release Semantics"):
//   The data slot itself is a plain, non-atomic T. What makes it safe to read on the other thread is the
//   *publication* pattern: write the data, then store the index with `release`; the other side loads the
//   index with `acquire`, then reads the data. Release/acquire on the same atomic forms a
//   "synchronizes-with" edge: everything the releasing thread wrote BEFORE its release store is guaranteed
//   visible to the acquiring thread AFTER its acquire load that saw that value. Without it, the compiler or
//   the CPU may reorder the slot write after the index store (ARM does this in hardware; x86 doesn't
//   reorder stores with stores, but the *compiler* still may), and the consumer would read a half-written
//   or stale slot. Each order used below says which edge it is part of.
//   On x86 acquire loads and release stores compile to plain MOVs, so this costs nothing there; what it
//   buys is a correct program on every CPU and under every optimizer.
//
// T must be trivially copyable: slots are overwritten by plain assignment, never destroyed, and popping
// copies bytes out. Trivially copyable types have no destructors or allocating copy constructors, so
// push/pop can never allocate, lock or throw. (MidiMessage, fss::Event: fine. std::string: rejected.)
//
// Real-time safe: no allocation (std::array storage lives inside the object), no locks, no exceptions,
// no loops. push() on a full queue returns false instead of blocking: the producer decides what to do
// (drop and count, retry later). Never retry in a loop on the audio thread.
//
// Compare with [MOODYCAMEL] ReaderWriterQueue: it grows by allocating new blocks (we never allocate),
// and it caches the other side's index in a producer-/consumer-local variable so most push/pop calls don't
// touch the other thread's cache line at all. That cache is a good L2.6 optimization if profiling ever
// shows the queue; here clarity wins.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace fss::util {

// Bytes per cache line on every x86-64 and most ARM64 cores. C++17 offers
// std::hardware_destructive_interference_size for this, but GCC warns that its value may differ between
// compiler flags (an ABI hazard for a type used across libraries), so a documented constant is simpler.
// (Apple M-series cores use 128-byte lines; padding to 64 there is a mild performance loss, not a bug.)
inline constexpr std::size_t kCacheLineBytes = 64;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // "structure was padded due to alignment specifier": that's the point.
#endif

template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2, "SpscQueue: Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "SpscQueue: Capacity must be a power of two (we mask, not mod)");
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue: T must be trivially copyable (no allocation on push/pop)");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "SpscQueue: std::atomic<size_t> must be lock-free, or the 'lock-free' queue secretly locks");

public:
    static constexpr std::size_t kCapacity = Capacity;

    SpscQueue() = default;
    // Not copyable or movable: a queue is a meeting point between two threads that hold references to it.
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer thread only. Returns false (and changes nothing) if the queue is full.
    bool push(const T& item) noexcept
    {
        // relaxed: head_ is written only by this thread, so we always see our own latest value.
        const std::size_t head = head_.load(std::memory_order_relaxed);
        // acquire: pairs with the consumer's release store to tail_ in pop(). It guarantees the consumer
        // has FINISHED reading a slot before we see that slot as free and overwrite it. Without acquire,
        // our write below could be reordered before this load and clobber data still being copied out.
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail == Capacity)
            return false; // full: never block, let the caller decide
        buf_[head & kMask] = item;
        // release: publishes the slot write above. Pairs with the consumer's acquire load of head_ in
        // pop(): once the consumer sees head + 1, it is guaranteed to see the item we just stored.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Returns false (and leaves `out` untouched) if the queue is empty.
    bool pop(T& out) noexcept
    {
        // relaxed: tail_ is written only by this thread.
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        // acquire: pairs with the producer's release store to head_ in push(), so the slot contents the
        // producer wrote before publishing are visible to the read below.
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (head == tail)
            return false; // empty
        out = buf_[tail & kMask];
        // release: tells the producer "I'm done reading this slot". Pairs with the producer's acquire load
        // of tail_ in push(), so the slot can't be overwritten while (or before) we copy it out.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Approximate: the other thread may change it the moment after we read. Fine for meters and tests,
    // never use it to decide whether push/pop will succeed (just call them; they report).
    std::size_t sizeApprox() const noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_acquire);
        return head - tail; // can't underflow: tail was loaded first and never passes head
    }

    bool emptyApprox() const noexcept { return sizeApprox() == 0; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Each index on its own cache line. Cores keep caches coherent per *line*: if head_ and tail_ shared
    // one, every push would invalidate the consumer's copy of tail_ and vice versa ("false sharing"),
    // bouncing the line between cores even though the threads never touch the same variable.
    alignas(kCacheLineBytes) std::atomic<std::size_t> head_{0}; // next slot to write; written by producer only
    alignas(kCacheLineBytes) std::atomic<std::size_t> tail_{0}; // next slot to read; written by consumer only
    alignas(kCacheLineBytes) std::array<T, Capacity> buf_{};    // own line(s), away from both indices
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace fss::util
