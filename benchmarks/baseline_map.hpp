// Baseline interface, namespace and include-path changes only. Original in provenance/cstl.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <cstl/detail/phmap/phmap.h>
#include <cstl/detail/hash_observers.hpp>

namespace cstl_baseline {

// Callbacks execute under one shard lock. Do not re-enter this container,
// acquire another container's locks, or retain references beyond the callback.
template <class Key, class T, class Hash = std::hash<Key>,
          class KeyEqual = std::equal_to<Key>, std::size_t ShardBits = 4>
class concurrent_unordered_map {
    static_assert(ShardBits <= 8, "ShardBits must be between 0 and 8");
    using storage_type = phmap::parallel_flat_hash_map<
        Key, T, Hash, KeyEqual, std::allocator<std::pair<const Key, T>>,
        ShardBits, std::shared_mutex>;
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
    bool insert(const value_type& value) { return data_.insert(value).second; }
    bool insert(value_type&& value) { return data_.insert(std::move(value)).second; }

    template <class... Args>
    bool try_emplace(const Key& key, Args&&... args) {
        return data_.try_emplace(key, std::forward<Args>(args)...).second;
    }
    template <class... Args>
    bool try_emplace(Key&& key, Args&&... args) {
        return data_.try_emplace(std::move(key), std::forward<Args>(args)...).second;
    }

    template <class U>
    bool insert_or_assign(const Key& key, U&& value) {
        return data_.insert_or_assign(key, std::forward<U>(value)).second;
    }

    // Atomic with respect to operations on this key. Callback runs only if
    // the key already exists; otherwise construct T from args under the lock.
    template <class F, class... Args>
    bool try_emplace_or_visit(const Key& key, F&& on_existing, Args&&... args) {
        return data_.try_emplace_l(key, [&](auto& entry) {
            std::invoke(std::forward<F>(on_existing), entry.second);
        }, std::forward<Args>(args)...);
    }

    template <class F>
    bool visit(const Key& key, F&& callback) {
        return data_.modify_if(key, [&](auto& entry) {
            std::invoke(std::forward<F>(callback), entry.second);
        });
    }
    template <class F>
    bool cvisit(const Key& key, F&& callback) const {
        return data_.if_contains(key, [&](const auto& entry) {
            std::invoke(std::forward<F>(callback), std::as_const(entry.second));
        });
    }

    // Copy-out lookup; instantiated only when T is copy-constructible.
    [[nodiscard]] std::optional<T> find(const Key& key) const {
        std::optional<T> result;
        cvisit(key, [&](const T& value) { result.emplace(value); });
        return result;
    }
    [[nodiscard]] bool contains(const Key& key) const {
        return cvisit(key, [](const T&) {});
    }
    size_type erase(const Key& key) { return data_.erase(key); }

    template <class Predicate>
    bool erase_if(const Key& key, Predicate&& predicate) {
        return data_.erase_if(key, [&](const auto& entry) {
            return std::invoke(std::forward<Predicate>(predicate), entry.second);
        });
    }

    [[nodiscard]] size_type size() const { return cstl::detail::locked_size(data_); }
    [[nodiscard]] bool empty() const { return size() == 0; }
    void reserve(size_type expected_size) { cstl::detail::locked_reserve(data_, expected_size); }
    void clear() { data_.clear(); }

    // Weakly consistent copy: each shard is locked separately, not all at once.
    [[nodiscard]] std::vector<std::pair<Key, T>> snapshot() const {
        std::vector<std::pair<Key, T>> result;
        data_.for_each([&](const auto& entry) {
            result.emplace_back(entry.first, entry.second);
        });
        return result;
    }
};

} // namespace cstl
