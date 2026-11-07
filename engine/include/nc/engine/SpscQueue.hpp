#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace nc::engine
{
/** Bounded single-producer / single-consumer lock-free queue.

    The message thread is the only producer, the audio callback the only
    consumer (or the reverse). No allocation after construction, no mutex,
    no blocking: a full queue refuses the push so the producer can report
    "host busy" instead of stalling the audio thread. Capacity is rounded
    up to a power of two so wrap-around is a mask, not a division.

    This replaces the ad-hoc command rail that used to live inside
    AudioEngine (fixed array + separate read/write atomics) so any engine
    caller — clip commands, MIDI clips, transport seeks — shares one
    tested implementation. */
template <typename T>
class SpscQueue
{
public:
    explicit SpscQueue (std::size_t capacity)
        : slots_ (roundUpToPowerOfTwo (capacity < 2 ? 2 : capacity))
        , mask_ (slots_.size() - 1)
    {
    }

    SpscQueue (const SpscQueue&) = delete;
    SpscQueue& operator= (const SpscQueue&) = delete;

    std::size_t capacity() const noexcept { return slots_.size(); }

    std::size_t size() const noexcept
    {
        const auto write = write_.load (std::memory_order_relaxed);
        const auto read = read_.load (std::memory_order_acquire);
        return static_cast<std::size_t> (write - read);
    }

    bool empty() const noexcept { return size() == 0; }

    /** Producer side. False when full; the slot is left untouched. */
    bool tryPush (const T& value)
    {
        const auto write = write_.load (std::memory_order_relaxed);
        const auto read = read_.load (std::memory_order_acquire);

        if (write - read >= slots_.size())
            return false;

        slots_[static_cast<std::size_t> (write) & mask_] = value;
        write_.store (write + 1, std::memory_order_release);
        return true;
    }

    bool tryPush (T&& value)
    {
        const auto write = write_.load (std::memory_order_relaxed);
        const auto read = read_.load (std::memory_order_acquire);

        if (write - read >= slots_.size())
            return false;

        slots_[static_cast<std::size_t> (write) & mask_] = std::move (value);
        write_.store (write + 1, std::memory_order_release);
        return true;
    }

    /** Consumer side. False when empty; `out` is left untouched. */
    bool tryPop (T& out)
    {
        const auto read = read_.load (std::memory_order_relaxed);
        const auto write = write_.load (std::memory_order_acquire);

        if (read == write)
            return false;

        out = slots_[static_cast<std::size_t> (read) & mask_];
        read_.store (read + 1, std::memory_order_release);
        return true;
    }

private:
    static std::size_t roundUpToPowerOfTwo (std::size_t value) noexcept
    {
        std::size_t result = 2;
        while (result < value)
            result <<= 1;
        return result;
    }

    std::vector<T> slots_;
    std::size_t mask_;

    // Head and tail on separate cache lines: the producer writes one and
    // the consumer the other, so false sharing would be pure overhead.
    alignas (64) std::atomic<std::uint64_t> write_ { 0 };
    alignas (64) std::atomic<std::uint64_t> read_ { 0 };
};
} // namespace nc::engine
