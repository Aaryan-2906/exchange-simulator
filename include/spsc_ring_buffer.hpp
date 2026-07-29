#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace exsim {

// SPSCRingBuffer: a fixed-capacity circular buffer safe for EXACTLY ONE
// producer thread and EXACTLY ONE consumer thread to use concurrently,
// with no locks/mutexes at all.
//
// WHY NO LOCKS ARE NEEDED HERE (read this before the code):
// A mutex works by making one thread wait for another. That's overkill
// when there are only ever two threads, and each one only ever touches
// ITS OWN index:
//   - the producer only ever WRITES to `head_` (and reads `tail_`)
//   - the consumer only ever WRITES to `tail_` (and reads `head_`)
// Because each index has exactly one writer, we don't need a lock to
// protect the WRITE. We DO need something to make sure that when the
// consumer reads `head_`, it sees the producer's most recent write (and
// vice versa) -- that's what std::atomic + acquire/release memory
// ordering gives us, without ever blocking a thread.
//
// MEMORY ORDERING, IN PLAIN WORDS:
//   - producer does: [write the actual data into the slot] THEN
//                     [atomically publish the new head_ with RELEASE]
//   - consumer does: [atomically read head_ with ACQUIRE] THEN
//                     [read the data from the slot]
// "release" on the write and "acquire" on the read form a pair: they
// guarantee that if the consumer's acquire-read sees the producer's
// release-write, then the consumer is ALSO guaranteed to see every
// ordinary (non-atomic) memory write the producer did before that
// release -- in this case, the actual order data written into the slot.
// Without this pairing, the compiler or CPU would be free to reorder
// those writes, and the consumer could read a slot that "looks" updated
// but whose actual data isn't there yet (a classic, nasty lock-free bug).
//
// Capacity MUST be a power of two -- this lets us turn "index modulo
// capacity" (a division, relatively slow) into "index AND (capacity-1)"
// (a bitwise AND, very fast). This is a real, common trick in low-latency
// ring buffer implementations, not just a style choice.
template <typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    SPSCRingBuffer() : head_(0), tail_(0) {}

    // Producer-side. Returns false if the buffer is full (caller decides
    // whether to spin/retry or drop -- see README for that discussion).
    bool try_push(const T& item) {
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) & kMask;

        // If advancing head would collide with tail, the buffer is full.
        // Acquire here because we're about to decide based on the
        // consumer's progress, and if we proceed we need to know the
        // slot we're about to overwrite is truly free.
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }

        buffer_[current_head] = item;

        // Release: publish the new head AFTER the data write above, so
        // the consumer's acquire-load of head_ is guaranteed to also see
        // this data write.
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    // Consumer-side. Returns false if the buffer is empty.
    bool try_pop(T& out) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);

        // Acquire: pair with the producer's release-store of head_, so
        // if we see the updated head_, we're also guaranteed to see the
        // data the producer wrote into that slot.
        if (current_tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }

        out = buffer_[current_tail];

        // Release: publish that this slot is now free for the producer
        // to reuse, after we've finished reading it above.
        tail_.store((current_tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Approximate size -- only safe as a rough diagnostic, NOT for
    // correctness decisions, since both indices can move concurrently
    // between the two loads below.
    size_t approx_size() const {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & kMask;
    }

private:
    static constexpr size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_;

    // alignas(64) prevents "false sharing": if head_ and tail_ landed on
    // the SAME CPU cache line, the producer writing head_ would force the
    // consumer's core to re-fetch that cache line even though it only
    // cares about tail_ (and vice versa) -- the two cores would be
    // invisibly fighting over one cache line despite touching different
    // variables. Padding them onto separate 64-byte cache lines (the
    // typical cache line size on modern x86/ARM) means the producer and
    // consumer's atomic writes never invalidate each other's cache line.
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

} // namespace exsim
