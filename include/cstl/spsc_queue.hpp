// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include "detail/folly/producer_consumer_queue.hpp"

namespace cstl {

// Exactly one producer and one consumer. A third observer may query size_approx.
template <class T>
class spsc_queue {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "spsc_queue requires lock-free 64-bit atomics on this target");
    static_assert(std::is_nothrow_move_assignable_v<T>,
                  "spsc_queue requires non-throwing move assignment for try_pop");
    static_assert(std::is_nothrow_destructible_v<T>, "T destructor must not throw");
    detail::folly_port::ProducerConsumerQueue<T> data_;
    static std::uint32_t checked_capacity(std::size_t capacity) {
        if (capacity < 1 || capacity > (std::uint64_t{1} << 31))
            throw std::invalid_argument("spsc_queue capacity must be in [1, 2^31]");
        return static_cast<std::uint32_t>(capacity);
    }

public:
    using value_type = T;
    using size_type = std::size_t;
    explicit spsc_queue(size_type capacity) : data_(checked_capacity(capacity)) {}
    spsc_queue(const spsc_queue&) = delete;
    spsc_queue& operator=(const spsc_queue&) = delete;
    spsc_queue(spsc_queue&&) = delete;
    spsc_queue& operator=(spsc_queue&&) = delete;

    [[nodiscard]] bool try_push(const T& value) { return data_.write(value); }
    [[nodiscard]] bool try_push(T&& value) { return data_.write(std::move(value)); }
    template <class... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) { return data_.write(std::forward<Args>(args)...); }
    [[nodiscard]] bool try_pop(T& out) { return data_.read(out); }
    [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
    [[nodiscard]] size_type size_approx() const noexcept { return data_.sizeGuess(); }
};

} // namespace cstl
