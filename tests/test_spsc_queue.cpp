// Tests for fss/util/spsc_queue.h (L2.3).
#include <fss/util/spsc_queue.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using fss::util::SpscQueue;

TEST_CASE("SpscQueue: empty pop fails and leaves the output alone", "[spsc]")
{
    SpscQueue<int, 4> q;
    int out = 42;
    CHECK(q.emptyApprox());
    CHECK_FALSE(q.pop(out));
    CHECK(out == 42);
}

TEST_CASE("SpscQueue: holds exactly Capacity items, then push fails", "[spsc]")
{
    SpscQueue<int, 4> q;
    for (int i = 0; i < 4; ++i)
        CHECK(q.push(i));
    CHECK(q.sizeApprox() == 4);
    CHECK_FALSE(q.push(99)); // full: all 4 slots are usable (monotonic indices, no wasted slot)

    int out = -1;
    REQUIRE(q.pop(out));
    CHECK(out == 0);
    CHECK(q.push(4)); // one slot freed, one push succeeds
    CHECK_FALSE(q.push(5));
}

TEST_CASE("SpscQueue: FIFO order", "[spsc]")
{
    SpscQueue<int, 8> q;
    for (int i = 10; i < 15; ++i)
        REQUIRE(q.push(i));
    for (int i = 10; i < 15; ++i)
    {
        int out = 0;
        REQUIRE(q.pop(out));
        CHECK(out == i);
    }
    int out = 0;
    CHECK_FALSE(q.pop(out));
}

TEST_CASE("SpscQueue: wraps around the ring many times", "[spsc]")
{
    // Capacity 4, pushing 3 and popping 3 per round: the slot index walks through every position and
    // wraps 750 times; a masking bug would show up as a wrong value or a spurious full/empty.
    SpscQueue<std::uint32_t, 4> q;
    std::uint32_t next = 0, expect = 0;
    for (int round = 0; round < 1000; ++round)
    {
        for (int i = 0; i < 3; ++i)
            REQUIRE(q.push(next++));
        for (int i = 0; i < 3; ++i)
        {
            std::uint32_t out = 0;
            REQUIRE(q.pop(out));
            REQUIRE(out == expect++);
        }
        REQUIRE(q.emptyApprox());
    }
}

TEST_CASE("SpscQueue: two-thread stress, strict order, nothing lost or duplicated", "[spsc][threads]")
{
    // The producer pushes 1..N in order, retrying when full; the consumer pops and checks that each
    // value is exactly previous + 1. Any lost item, duplicate, reorder or torn read breaks the chain.
    // A small capacity (1024) keeps the queue flipping between full and empty, which is where ordering
    // bugs live. This can't PROVE correctness (only reasoning about the memory model does that), but a
    // missing acquire/release reliably fails here on ARM, and a masking or full/empty bug fails anywhere.
    constexpr std::uint64_t kN = 5'000'000;
    static SpscQueue<std::uint64_t, 1024> q; // static: 8 KB of slots plus padding, kept off the stack

    const auto start = std::chrono::steady_clock::now();

    std::thread producer([] {
        for (std::uint64_t v = 1; v <= kN; ++v)
            while (!q.push(v))
                std::this_thread::yield(); // full: let the consumer catch up (fine in a test, never on audio)
    });

    std::uint64_t expected = 1;
    std::uint64_t errors = 0;
    while (expected <= kN)
    {
        std::uint64_t v = 0;
        if (!q.pop(v))
        {
            std::this_thread::yield();
            continue;
        }
        if (v != expected)
            ++errors; // counted, not REQUIREd: Catch2 assertions are not meant for tight loops
        expected = v + 1; // resync so one error doesn't cascade into millions
    }
    producer.join();

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("SpscQueue stress: %llu items in %.3f s (%.1f M items/s)\n",
                static_cast<unsigned long long>(kN), seconds, static_cast<double>(kN) / seconds * 1e-6);

    CHECK(errors == 0);
    std::uint64_t extra = 0;
    CHECK_FALSE(q.pop(extra)); // nothing duplicated past the end
}
