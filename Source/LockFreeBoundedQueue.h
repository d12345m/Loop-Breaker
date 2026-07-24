#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/**
    Fixed-capacity lock-free queue based on Dmitry Vyukov's bounded MPMC
    sequence-number algorithm.

    Loop Breaker uses it as an MPSC queue: UI, scheduler, and host-transport
    producers publish immutable commands while the render callback is the only
    consumer. No allocation, lock, or retry loop with an unbounded duration is
    performed on the audio thread.
*/
template <typename Item, std::size_t Capacity>
class LockFreeBoundedQueue
{
    static_assert (Capacity >= 2, "Queue capacity must be at least two");
    static_assert ((Capacity & (Capacity - 1)) == 0,
                   "Queue capacity must be a power of two");
    static_assert (std::is_trivially_copyable<Item>::value,
                   "Queue items must be trivially copyable");

public:
    LockFreeBoundedQueue() noexcept
    {
        for (std::size_t index = 0; index < Capacity; ++index)
            cells[index].sequence.store (index, std::memory_order_relaxed);
    }

    bool tryEnqueue (const Item& item) noexcept
    {
        std::size_t position = enqueuePosition.load (std::memory_order_relaxed);

        for (std::size_t attempt = 0; attempt < Capacity; ++attempt)
        {
            Cell& cell = cells[position & mask];
            const std::size_t sequence = cell.sequence.load (std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t> (sequence)
                                  - static_cast<std::intptr_t> (position);

            if (difference == 0)
            {
                if (enqueuePosition.compare_exchange_weak (
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed))
                {
                    cell.item = item;
                    cell.sequence.store (position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false;
            }
            else
            {
                position = enqueuePosition.load (std::memory_order_relaxed);
            }
        }

        // Contention can make a lock-free CAS loop arbitrarily long in theory.
        // Treat exhausting the fixed retry budget like a full queue so callers
        // on a host callback always have a hard execution bound.
        return false;
    }

    bool tryDequeue (Item& item) noexcept
    {
        std::size_t position = dequeuePosition.load (std::memory_order_relaxed);

        for (std::size_t attempt = 0; attempt < Capacity; ++attempt)
        {
            Cell& cell = cells[position & mask];
            const std::size_t sequence = cell.sequence.load (std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t> (sequence)
                                  - static_cast<std::intptr_t> (position + 1);

            if (difference == 0)
            {
                if (dequeuePosition.compare_exchange_weak (
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed))
                {
                    item = cell.item;
                    cell.sequence.store (position + Capacity, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false;
            }
            else
            {
                position = dequeuePosition.load (std::memory_order_relaxed);
            }
        }

        return false;
    }

private:
    static constexpr std::size_t mask = Capacity - 1;

    struct Cell
    {
        std::atomic<std::size_t> sequence { 0 };
        Item item {};
    };

    std::array<Cell, Capacity> cells {};
    alignas (64) std::atomic<std::size_t> enqueuePosition { 0 };
    alignas (64) std::atomic<std::size_t> dequeuePosition { 0 };
};
