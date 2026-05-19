#pragma once

#include <atomic>
#include <algorithm>

// Lock-free single-producer single-consumer ring buffer for float samples.
// Capacity must be a power of 2. push() is safe from the producer/audio thread;
// pop() and available() are safe from the consumer/worker thread.
// Uses monotonically-growing unsigned indices for correct wrap-around arithmetic.
template<unsigned Capacity>
class FloatRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr unsigned kMask = Capacity - 1;

public:
    FloatRingBuffer() : writeIdx_(0u), readIdx_(0u) {}

    // Push up to n samples from src. Returns actual count written (may be < n if full).
    // Safe to call from the audio/producer thread.
    int push(const float* src, int n) {
        const unsigned w = writeIdx_.load(std::memory_order_relaxed);
        const unsigned r = readIdx_.load(std::memory_order_acquire);
        const unsigned space = Capacity - (w - r);
        const unsigned count = static_cast<unsigned>(std::min<int>(n, static_cast<int>(space)));
        if (count == 0) return 0;

        const unsigned startSlot = w & kMask;
        const unsigned first = std::min(count, Capacity - startSlot);
        const unsigned second = count - first;

        std::copy(src, src + first, data_ + startSlot);
        if (second > 0) {
            std::copy(src + first, src + first + second, data_);
        }

        writeIdx_.store(w + count, std::memory_order_release);
        return static_cast<int>(count);
    }

    // Pop exactly n samples into dst. Returns n on success, 0 if not enough data.
    // Safe to call from the consumer/worker thread.
    int pop(float* dst, int n) {
        const unsigned r = readIdx_.load(std::memory_order_relaxed);
        const unsigned w = writeIdx_.load(std::memory_order_acquire);
        const unsigned avail = w - r;
        if (avail < static_cast<unsigned>(n)) return 0;

        const unsigned startSlot = r & kMask;
        const unsigned first = std::min<unsigned>(static_cast<unsigned>(n), Capacity - startSlot);
        const unsigned second = static_cast<unsigned>(n) - first;

        std::copy(data_ + startSlot, data_ + startSlot + first, dst);
        if (second > 0) {
            std::copy(data_, data_ + second, dst + first);
        }

        readIdx_.store(r + static_cast<unsigned>(n), std::memory_order_release);
        return n;
    }

    // Samples currently available to read. Safe from any thread.
    int available() const {
        const unsigned w = writeIdx_.load(std::memory_order_acquire);
        const unsigned r = readIdx_.load(std::memory_order_relaxed);
        return static_cast<int>(w - r);
    }

private:
    float data_[Capacity];
    std::atomic<unsigned> writeIdx_;
    std::atomic<unsigned> readIdx_;
};
