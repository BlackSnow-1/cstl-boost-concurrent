// Copyright (c) 2006-2018 Maxim Khizhinsky
// Distributed under the Boost Software License, Version 1.0.
// See licenses/BOOST-1.0.txt.
//
// Core reservation/publication loops adapted from libcds VyukovMPMCCycleQueue.
// Original algorithm: Copyright (c) 2010-2011 Dmitry Vyukov.
// See licenses/VYUKOV-BSD.txt for the original algorithm's license.
// CSTL changes (2026-11-04): standard C++ atomics/storage, explicit lifetimes,
// checked capacity, unsigned difference before conversion, no-throw payload
// operations, fixed backoff, removed libcds traits/GC/optional counters.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // Intentional cache-line padding.
#endif

namespace cstl {

// Bounded MPMC queue. No dynamic queue-storage allocation after construction.
// Not formally lock-free: a preempted slot owner can stall other operations.
template <class T>
class bounded_queue {
    static_assert(std::is_nothrow_destructible_v<T>, "T destructor must not throw");
    struct cell_type {
        std::atomic<std::size_t> sequence{0};
        alignas(T) unsigned char storage[sizeof(T)];
        T* value() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    };
    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<cell_type[]> cells_;
    alignas(64) std::atomic<std::size_t> enqueue_{0};
    alignas(64) std::atomic<std::size_t> dequeue_{0};

    static std::size_t check_capacity(std::size_t capacity) {
        if (capacity < 2 || (capacity & (capacity - 1)) != 0 ||
            capacity > static_cast<std::size_t>((std::numeric_limits<std::intptr_t>::max)()) / 2)
            throw std::invalid_argument("bounded_queue capacity must be a power of two >= 2 and within counter range");
        return capacity;
    }

    template <class F>
    bool consume(F&& callback) noexcept {
        cell_type* cell;
        auto pos = dequeue_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & mask_];
            const auto seq = cell->sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(seq - (pos + 1));
            if (difference == 0) {
                if (dequeue_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
            } else if (difference < 0) {
                if (pos - enqueue_.load(std::memory_order_relaxed) == 0) return false;
                std::this_thread::yield();
                pos = dequeue_.load(std::memory_order_relaxed);
            } else {
                pos = dequeue_.load(std::memory_order_relaxed);
            }
        }
        callback(*cell->value());
        cell->value()->~T();
        cell->sequence.store(pos + capacity_, std::memory_order_release);
        return true;
    }

public:
    using value_type = T;
    using size_type = std::size_t;
    explicit bounded_queue(size_type capacity)
        : capacity_(check_capacity(capacity)), mask_(capacity_ - 1),
          cells_(new cell_type[capacity_]) {
        for (size_type i = 0; i < capacity_; ++i) cells_[i].sequence.store(i, std::memory_order_relaxed);
    }
    bounded_queue(const bounded_queue&) = delete;
    bounded_queue& operator=(const bounded_queue&) = delete;
    bounded_queue(bounded_queue&&) = delete;
    bounded_queue& operator=(bounded_queue&&) = delete;
    ~bounded_queue() { while (consume([](T&) noexcept {})) {} }

    template <class... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept {
        static_assert(std::is_nothrow_constructible_v<T, Args&&...>,
                      "construction after slot reservation must not throw");
        cell_type* cell;
        auto pos = enqueue_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & mask_];
            const auto seq = cell->sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(seq - pos);
            if (difference == 0) {
                if (enqueue_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) break;
            } else if (difference < 0) {
                if (pos - dequeue_.load(std::memory_order_relaxed) == capacity_) return false;
                std::this_thread::yield();
                pos = enqueue_.load(std::memory_order_relaxed);
            } else {
                pos = enqueue_.load(std::memory_order_relaxed);
            }
        }
        ::new (static_cast<void*>(cell->storage)) T(std::forward<Args>(args)...);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool try_push(const T& value) noexcept { return try_emplace(value); }
    [[nodiscard]] bool try_push(T&& value) noexcept { return try_emplace(std::move(value)); }
    [[nodiscard]] bool try_pop(T& out) noexcept {
        static_assert(std::is_nothrow_move_assignable_v<T>, "try_pop requires non-throwing move assignment");
        return consume([&](T& value) noexcept { out = std::move(value); });
    }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
};

} // namespace cstl

#ifdef _MSC_VER
#pragma warning(pop)
#endif
