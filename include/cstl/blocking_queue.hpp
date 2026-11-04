// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstddef>
#include <utility>

#include "detail/moodycamel/blockingconcurrentqueue.h"

namespace cstl {

// No close/shutdown operation. Join all waiters before destruction.
template <class T>
class blocking_queue {
    moodycamel::BlockingConcurrentQueue<T> data_;

public:
    using value_type = T;
    using size_type = std::size_t;

    explicit blocking_queue(size_type initial_capacity = 32) : data_(initial_capacity) {}
    blocking_queue(const blocking_queue&) = delete;
    blocking_queue& operator=(const blocking_queue&) = delete;
    blocking_queue(blocking_queue&&) = delete;
    blocking_queue& operator=(blocking_queue&&) = delete;

    [[nodiscard]] bool push(const T& value) { return data_.enqueue(value); }
    [[nodiscard]] bool push(T&& value) { return data_.enqueue(std::move(value)); }
    [[nodiscard]] bool try_pop(T& out) { return data_.try_dequeue(out); }
    void wait_pop(T& out) { data_.wait_dequeue(out); }
    template <class Rep, class Period>
    [[nodiscard]] bool wait_pop_for(T& out, std::chrono::duration<Rep, Period> timeout) {
        // Upstream treats negative timeout values as an indefinite wait.
        if (timeout <= timeout.zero()) { return try_pop(out); }
        return data_.wait_dequeue_timed(out, timeout);
    }
    [[nodiscard]] size_type size_approx() const { return data_.size_approx(); }
};

} // namespace cstl
