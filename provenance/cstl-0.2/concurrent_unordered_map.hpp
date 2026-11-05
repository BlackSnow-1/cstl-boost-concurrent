// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <boost/unordered/concurrent_flat_map.hpp>
#include "detail/map_value_visitor.hpp"

namespace cstl {

// Callbacks execute under Boost group access protection. Do not re-enter this container,
// acquire another container's locks, or retain references beyond the callback.
template <class Key, class T, class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>, std::size_t ShardBits = 4>
class concurrent_unordered_map {
    static_assert(ShardBits <= 8, "ShardBits must be between 0 and 8");
    // ShardBits remains source-compatible; Boost controls its own lock granularity.
    using storage_type = boost::concurrent_flat_map<Key, T, Hash, KeyEqual>;
    storage_type data_;

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;

    concurrent_unordered_map() = default;
    explicit concurrent_unordered_map(size_type expected_size) { reserve(expected_size); }
    concurrent_unordered_map(const concurrent_unordered_map&) = delete;
    concurrent_unordered_map& operator=(const concurrent_unordered_map&) = delete;
    concurrent_unordered_map(concurrent_unordered_map&&) = delete;
    concurrent_unordered_map& operator=(concurrent_unordered_map&&) = delete;

    // Return true only when a new key was inserted. No iterator escapes a lock.
    BOOST_FORCEINLINE bool insert(const value_type& value) { return data_.insert(value); }
    BOOST_FORCEINLINE bool insert(value_type&& value) { return data_.insert(std::move(value)); }

    template <class... Args>
    BOOST_FORCEINLINE bool try_emplace(const Key& key, Args&&... args) {
        return data_.try_emplace(key, std::forward<Args>(args)...);
    }
    template <class... Args>
    BOOST_FORCEINLINE bool try_emplace(Key&& key, Args&&... args) {
        return data_.try_emplace(std::move(key), std::forward<Args>(args)...);
    }

    template <class U>
    BOOST_FORCEINLINE bool insert_or_assign(const Key& key, U&& value) {
        return data_.insert_or_assign(key, std::forward<U>(value));
    }

    // Atomic with respect to operations on this key. Callback runs only if
    // the key already exists; otherwise construct T from args under the lock.
    template <class F, class... Args>
    BOOST_FORCEINLINE bool try_emplace_or_visit(const Key& key, F&& on_existing, Args&&... args) {
        return data_.try_emplace_or_visit(key, std::forward<Args>(args)...,
            detail::map_value_visitor<F>{std::forward<F>(on_existing)});
    }

    template <class F>
    BOOST_FORCEINLINE bool visit(const Key& key, F&& callback) {
        return data_.visit(key, detail::map_value_visitor<F>{std::forward<F>(callback)});
    }
    template <class F>
    BOOST_FORCEINLINE bool cvisit(const Key& key, F&& callback) const {
        return data_.cvisit(key, detail::map_value_visitor<F>{std::forward<F>(callback)});
    }

    // Copy-out lookup; instantiated only when T is copy-constructible.
    [[nodiscard]] BOOST_FORCEINLINE std::optional<T> find(const Key& key) const {
        std::optional<T> result;
        cvisit(key, [&](const T& value) { result.emplace(value); });
        return result;
    }
    [[nodiscard]] BOOST_FORCEINLINE bool contains(const Key& key) const {
        return cvisit(key, [](const T&) {});
    }
    BOOST_FORCEINLINE size_type erase(const Key& key) { return data_.erase(key); }

    template <class Predicate>
    BOOST_FORCEINLINE bool erase_if(const Key& key, Predicate&& predicate) {
        return data_.erase_if(key, detail::map_value_visitor<Predicate>{std::forward<Predicate>(predicate)});
    }

    [[nodiscard]] size_type size() const { return data_.size(); }
    [[nodiscard]] bool empty() const { return size() == 0; }
    void reserve(size_type expected_size) { data_.reserve(expected_size); }
    void clear() { data_.clear(); }

    // Weakly consistent copy: Boost visit_all does not lock the entire table at once.
    [[nodiscard]] std::vector<std::pair<Key, T>> snapshot() const {
        std::vector<std::pair<Key, T>> result;
        data_.cvisit_all([&](const auto& entry) {
            result.emplace_back(entry.first, entry.second);
        });
        return result;
    }
};

} // namespace cstl
