#include <gtest/gtest.h>
#include "spsc_ring_buffer.hpp"

#include <thread>
#include <vector>

using namespace exsim;

TEST(SPSCRingBufferTest, PushAndPopSingleItem) {
    SPSCRingBuffer<int, 8> buf;
    EXPECT_TRUE(buf.try_push(42));

    int out = 0;
    EXPECT_TRUE(buf.try_pop(out));
    EXPECT_EQ(out, 42);
}

TEST(SPSCRingBufferTest, PopOnEmptyBufferFails) {
    SPSCRingBuffer<int, 8> buf;
    int out = 0;
    EXPECT_FALSE(buf.try_pop(out));
}

TEST(SPSCRingBufferTest, PreservesFIFOOrder) {
    SPSCRingBuffer<int, 8> buf;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(buf.try_push(i));
    }
    for (int i = 0; i < 5; ++i) {
        int out = -1;
        EXPECT_TRUE(buf.try_pop(out));
        EXPECT_EQ(out, i);
    }
}

TEST(SPSCRingBufferTest, PushFailsWhenFull) {
    // Capacity 4 means only 3 usable slots (one slot always kept empty
    // to distinguish "full" from "empty" -- see header: next_head==tail
    // means full). This is a standard, deliberate ring-buffer trade-off.
    SPSCRingBuffer<int, 4> buf;
    EXPECT_TRUE(buf.try_push(1));
    EXPECT_TRUE(buf.try_push(2));
    EXPECT_TRUE(buf.try_push(3));
    EXPECT_FALSE(buf.try_push(4)); // full
}

TEST(SPSCRingBufferTest, WraparoundWorksCorrectly) {
    SPSCRingBuffer<int, 4> buf;
    // Fill, drain, fill again -- forces the internal indices to wrap
    // around the end of the underlying array multiple times.
    for (int round = 0; round < 5; ++round) {
        EXPECT_TRUE(buf.try_push(round * 10));
        EXPECT_TRUE(buf.try_push(round * 10 + 1));

        int out = -1;
        EXPECT_TRUE(buf.try_pop(out));
        EXPECT_EQ(out, round * 10);
        EXPECT_TRUE(buf.try_pop(out));
        EXPECT_EQ(out, round * 10 + 1);
    }
}

// --- The real test: two ACTUAL threads, no mutex, verified correct ---
//
// One producer thread pushes 0, 1, 2, ..., N-1 as fast as it can (spinning
// on try_push when the buffer is full). One consumer thread pops as fast
// as it can (spinning on try_pop when empty) and records every value it
// received. If the ring buffer has ANY race condition, this test will
// eventually show: a value out of order, a duplicate, or a gap -- run
// enough times / high enough N and a real bug WILL surface eventually.
TEST(SPSCRingBufferTest, ConcurrentProducerConsumerPreservesAllValuesInOrder) {
    constexpr size_t N = 200000;
    SPSCRingBuffer<size_t, 1024> buf;
    std::vector<size_t> received;
    received.reserve(N);

    std::thread producer([&buf]() {
        for (size_t i = 0; i < N; ++i) {
            while (!buf.try_push(i)) {
                std::this_thread::yield(); // spin -- buffer momentarily full
            }
        }
    });

    std::thread consumer([&buf, &received]() {
        size_t item;
        size_t received_count = 0;
        while (received_count < N) {
            if (buf.try_pop(item)) {
                received.push_back(item);
                ++received_count;
            } else {
                std::this_thread::yield(); // spin -- buffer momentarily empty
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), N);
    for (size_t i = 0; i < N; ++i) {
        // Every value must appear EXACTLY ONCE, in EXACTLY the order it
        // was pushed. Any race condition would corrupt this.
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}
