// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "detail/phmap/phmap.h"
#include "detail/hash_observers.hpp"

namespace cstl {

template <class Key, class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>, std::size_t ShardBits = 4>
class concurrent_unordered_set {
    static_assert(ShardBits <= 8, "ShardBits must be between 0 and 8");
    using storage_type = phmap::parallel_flat_hash_set<
        Key, Hash, KeyEqual, std::allocator<Key>, ShardBits, std::shared_mutex>;
    storage_type data_;

public:
    using key_type = Key;
    using value_type = Key;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;

    concurrent_unordered_set() = default;
    explicit concurrent_unordered_set(size_type expected_size) { reserve(expected_size); }
    concurrent_unordered_set(const concurrent_unordered_set&) = delete;
    concurrent_unordered_set& operator=(const concurrent_unordered_set&) = delete;
    concurrent_unordered_set(concurrent_unordered_set&&) = delete;
    concurrent_unordered_set& operator=(concurrent_unordered_set&&) = delete;

    bool insert(const Key& key) { return data_.insert(key).second; }
    bool insert(Key&& key) { return data_.insert(std::move(key)).second; }
    template <class... Args>
    bool emplace(Args&&... args) { return data_.emplace(std::forward<Args>(args)...).second; }

    template <class F>
    bool cvisit(const Key& key, F&& callback) const {
        return data_.if_contains(key, [&](const Key& value) {
            std::invoke(std::forward<F>(callback), value);
        });
    }
    [[nodiscard]] bool contains(const Key& key) const {
        return cvisit(key, [](const Key&) {});
    }
    size_type erase(const Key& key) { return data_.erase(key); }
    [[nodiscard]] size_type size() const { return detail::locked_size(data_); }
    [[nodiscard]] bool empty() const { return size() == 0; }
    void reserve(size_type expected_size) { detail::locked_reserve(data_, expected_size); }
    void clear() { data_.clear(); }

    [[nodiscard]] std::vector<Key> snapshot() const {
        std::vector<Key> result;
        data_.for_each([&](const Key& key) { result.push_back(key); });
        return result;
    }
};

} // namespace cstl
