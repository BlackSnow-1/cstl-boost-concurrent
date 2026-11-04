// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>
#include "detail/tbb/priority_queue.hpp"

namespace cstl {

// Flat-combining heap from oneTBB. Default Compare gives a max-priority queue.
template <class T, class Compare = std::less<>>
class concurrent_priority_queue {
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>,
                  "priority queue requires non-throwing moves to keep the combiner live");
    static_assert(std::is_nothrow_destructible_v<T>, "T destructor must not throw");
    static_assert(std::is_nothrow_invocable_r_v<bool, Compare&, const T&, const T&>,
                  "priority queue comparator must not throw; use std::less<>/greater<> or a noexcept comparator");
    detail::tbb_port::concurrent_priority_queue<T, Compare> data_;

public:
    using value_type = T;
    using size_type = std::size_t;
    using value_compare = Compare;
    concurrent_priority_queue() = default;
    explicit concurrent_priority_queue(size_type initial_capacity, Compare compare = Compare{})
        : data_(initial_capacity, compare) {}
    concurrent_priority_queue(const concurrent_priority_queue&) = delete;
    concurrent_priority_queue& operator=(const concurrent_priority_queue&) = delete;
    concurrent_priority_queue(concurrent_priority_queue&&) = delete;
    concurrent_priority_queue& operator=(concurrent_priority_queue&&) = delete;

    void push(const T& value) {
        static_assert(std::is_copy_constructible_v<T>, "const-reference push requires copyable T");
        data_.push(value);
    }
    void push(T&& value) { data_.push(std::move(value)); }
    template <class... Args>
    void emplace(Args&&... args) { data_.emplace(std::forward<Args>(args)...); }
    [[nodiscard]] bool try_pop(T& out) { return data_.try_pop(out); }
    [[nodiscard]] size_type size_approx() const { return data_.size(); }
    // Requires external quiescence: no concurrent operation, including observers.
    void clear_unsafe() { data_.clear(); }
};

} // namespace cstl
