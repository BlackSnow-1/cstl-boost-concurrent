// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <utility>

#include "detail/moodycamel/concurrentqueue.h"

namespace cstl {

// MPMC queue. FIFO per producer; there is no global FIFO across producers.
// Construction/destruction must be externally synchronized.
template <class T>
class concurrent_queue {
    moodycamel::ConcurrentQueue<T> data_;

public:
    using value_type = T;
    using size_type = std::size_t;
    using producer_token_type = moodycamel::ProducerToken;
    using consumer_token_type = moodycamel::ConsumerToken;

    explicit concurrent_queue(size_type initial_capacity = 32) : data_(initial_capacity) {}
    concurrent_queue(const concurrent_queue&) = delete;
    concurrent_queue& operator=(const concurrent_queue&) = delete;
    concurrent_queue(concurrent_queue&&) = delete;
    concurrent_queue& operator=(concurrent_queue&&) = delete;

    [[nodiscard]] producer_token_type make_producer_token() { return producer_token_type(data_); }
    [[nodiscard]] consumer_token_type make_consumer_token() { return consumer_token_type(data_); }

    // May allocate; false means allocation/capacity failure. Never ignore it.
    [[nodiscard]] bool push(const T& value) { return data_.enqueue(value); }
    [[nodiscard]] bool push(T&& value) { return data_.enqueue(std::move(value)); }
    [[nodiscard]] bool push(const producer_token_type& token, const T& value) {
        return data_.enqueue(token, value);
    }
    [[nodiscard]] bool push(const producer_token_type& token, T&& value) {
        return data_.enqueue(token, std::move(value));
    }

    // No additional queue-block allocation; implicit producer setup may allocate.
    // Use a pre-created producer token to avoid that setup on this path.
    // May fail even if a conventional bounded queue would not be "full".
    [[nodiscard]] bool try_push(const T& value) { return data_.try_enqueue(value); }
    [[nodiscard]] bool try_push(T&& value) { return data_.try_enqueue(std::move(value)); }
    [[nodiscard]] bool try_push(const producer_token_type& token, const T& value) {
        return data_.try_enqueue(token, value);
    }
    [[nodiscard]] bool try_push(const producer_token_type& token, T&& value) {
        return data_.try_enqueue(token, std::move(value));
    }
    [[nodiscard]] bool try_pop(T& out) { return data_.try_dequeue(out); }
    [[nodiscard]] bool try_pop(consumer_token_type& token, T& out) {
        return data_.try_dequeue(token, out);
    }
    template <class InputIterator>
    [[nodiscard]] bool push_bulk(InputIterator first, size_type count) {
        return data_.enqueue_bulk(first, count);
    }
    template <class OutputIterator>
    size_type try_pop_bulk(OutputIterator first, size_type max_count) {
        return data_.try_dequeue_bulk(first, max_count);
    }
    [[nodiscard]] size_type size_approx() const { return data_.size_approx(); }
};

} // namespace cstl
