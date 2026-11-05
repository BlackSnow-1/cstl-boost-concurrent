// SPDX-License-Identifier: MIT
// Boost flat tables with shard-level synchronization and one hash per operation.
#pragma once
#include <array>
#include <cstddef>
#include <type_traits>
#include <boost/core/empty_value.hpp>
#include <boost/unordered/detail/mulx.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // Intentional cache-line alignment of shard locks.
#endif
namespace cstl
{
// Each callback runs under one shard lock; references must not escape it.
template <class Key, class T, class Hash = std::hash<Key>, class Equal = std::equal_to<Key>, std::size_t ShardBits = 4>
class concurrent_unordered_map
{
    static_assert(ShardBits <= 8, "ShardBits must be between 0 and 8");
    static constexpr std::size_t count = std::size_t{1} << ShardBits;
    template <class Ref> struct query
    {
        Ref key;
        std::size_t hash;
        operator Ref() const
        {
            return static_cast<Ref>(key);
        }
    };
    struct prepared_hash : boost::empty_value<Hash>
    {
        using base = boost::empty_value<Hash>;
        using is_transparent = void;
        using is_avalanching = std::true_type;
        // 15 slots at 14/15 fill grow at the original 16-slot table limit.
        static constexpr float cstl_max_load_factor = 14.0f / 15.0f;
        explicit prepared_hash(const Hash &h) : base(boost::empty_init, h)
        {
        }
        std::size_t operator()(const Key &k) const
        {
            return boost::unordered::detail::mulx(base::get()(k));
        }
        template <class R> std::size_t operator()(const query<R> &q) const noexcept
        {
            return q.hash;
        }
    };
    struct prepared_equal : boost::empty_value<Equal>
    {
        using base = boost::empty_value<Equal>;
        using is_transparent = void;
        explicit prepared_equal(const Equal &e) : base(boost::empty_init, e)
        {
        }
        static const Key &unwrap(const Key &k)
        {
            return k;
        }
        template <class R> static const Key &unwrap(const query<R> &q)
        {
            return q.key;
        }
        template <class A, class B> bool operator()(const A &a, const B &b) const
        {
            return base::get()(unwrap(a), unwrap(b));
        }
    };
    using table = boost::unordered_flat_map<Key, T, prepared_hash, prepared_equal>;
    struct alignas(64) shard
    {
        mutable std::shared_mutex mutex;
        table data;
        shard(const Hash &h, const Equal &e) : data(0, prepared_hash(h), prepared_equal(e))
        {
        }
    };
    Hash hash_{};
    Equal equal_{};
    template <std::size_t... I> std::array<shard, count> make_shards(std::index_sequence<I...>)
    {
        return {{(static_cast<void>(I), shard(hash_, equal_))...}};
    }
    std::array<shard, count> shards_ = make_shards(std::make_index_sequence<count>{});
    BOOST_FORCEINLINE std::size_t digest(const Key &k) const
    {
        return boost::unordered::detail::mulx(hash_(k));
    }
    static BOOST_FORCEINLINE std::size_t index(std::size_t h)
    {
        return ((h >> 8) ^ (h >> 16) ^ (h >> 24)) & (count - 1);
    }
    template <class K, class... A> BOOST_FORCEINLINE bool emplace(K &&k, A &&...a)
    {
        auto h = digest(k);
        auto &s = shards_[index(h)];
        std::unique_lock<std::shared_mutex> l(s.mutex);
        return s.data.try_emplace(query<K &&>{std::forward<K>(k), h}, std::forward<A>(a)...).second;
    }

  public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<const Key, T>;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = Equal;
    concurrent_unordered_map() = default;
    explicit concurrent_unordered_map(std::size_t n)
    {
        reserve(n);
    }
    concurrent_unordered_map(const concurrent_unordered_map &) = delete;
    concurrent_unordered_map &operator=(const concurrent_unordered_map &) = delete;
    concurrent_unordered_map(concurrent_unordered_map &&) = delete;
    concurrent_unordered_map &operator=(concurrent_unordered_map &&) = delete;
    template <class... A> BOOST_FORCEINLINE bool try_emplace(const Key &k, A &&...a)
    {
        return emplace(k, std::forward<A>(a)...);
    }
    template <class... A> BOOST_FORCEINLINE bool try_emplace(Key &&k, A &&...a)
    {
        return emplace(std::move(k), std::forward<A>(a)...);
    }
    bool insert(const value_type &v)
    {
        return try_emplace(v.first, v.second);
    }
    bool insert(value_type &&v)
    {
        return try_emplace(v.first, std::move(v.second));
    }
    template <class F> BOOST_FORCEINLINE bool visit(const Key &k, F &&f)
    {
        auto h = digest(k);
        auto &s = shards_[index(h)];
        std::unique_lock<std::shared_mutex> l(s.mutex);
        auto i = s.data.find(query<const Key &>{k, h});
        if (i == s.data.end())
            return false;
        std::invoke(std::forward<F>(f), i->second);
        return true;
    }
    template <class F> BOOST_FORCEINLINE bool cvisit(const Key &k, F &&f) const
    {
        auto h = digest(k);
        auto &s = shards_[index(h)];
        std::shared_lock<std::shared_mutex> l(s.mutex);
        auto i = s.data.find(query<const Key &>{k, h});
        if (i == s.data.end())
            return false;
        std::invoke(std::forward<F>(f), std::as_const(i->second));
        return true;
    }
    template <class F, class... A> BOOST_FORCEINLINE bool try_emplace_or_visit(const Key &k, F &&f, A &&...a)
    {
        auto h = digest(k);
        auto &s = shards_[index(h)];
        std::unique_lock<std::shared_mutex> l(s.mutex);
        auto result = s.data.try_emplace(query<const Key &>{k, h}, std::forward<A>(a)...);
        if (!result.second)
            std::invoke(std::forward<F>(f), result.first->second);
        return result.second;
    }
    template <class V> bool insert_or_assign(const Key &k, V &&v)
    {
        auto h = digest(k);
        auto &s = shards_[index(h)];
        std::unique_lock<std::shared_mutex> l(s.mutex);
        return s.data.insert_or_assign(query<const Key &>{k, h}, std::forward<V>(v)).second;
    }
    BOOST_FORCEINLINE std::size_t erase(const Key &k)
    {
        auto h = digest(k);
        auto &s = shards_[index(h)];
        std::unique_lock<std::shared_mutex> l(s.mutex);
        return s.data.erase(query<const Key &>{k, h});
    }
    template <class F> bool erase_if(const Key &k, F &&f)
    {
        auto h = digest(k);
        auto &s = shards_[index(h)];
        std::unique_lock<std::shared_mutex> l(s.mutex);
        auto i = s.data.find(query<const Key &>{k, h});
        if (i == s.data.end() || !std::invoke(std::forward<F>(f), std::as_const(i->second)))
            return false;
        s.data.erase(i);
        return true;
    }
    [[nodiscard]] auto find(const Key &k) const
    {
        std::optional<T> v;
        cvisit(k, [&](const T &x) { v.emplace(x); });
        return v;
    }
    [[nodiscard]] bool contains(const Key &k) const
    {
        return cvisit(k, [](const T &) {});
    }
    [[nodiscard]] std::size_t size() const
    {
        std::size_t n = 0;
        for (auto &s : shards_)
        {
            std::shared_lock<std::shared_mutex> l(s.mutex);
            n += s.data.size();
        }
        return n;
    }
    [[nodiscard]] bool empty() const
    {
        return size() == 0;
    }
    void reserve(std::size_t n)
    {
        for (auto &s : shards_)
        {
            std::unique_lock<std::shared_mutex> l(s.mutex);
            s.data.reserve(n / count + (n % count != 0));
        }
    }
    void clear()
    {
        for (auto &s : shards_)
        {
            std::unique_lock<std::shared_mutex> l(s.mutex);
            s.data.clear();
        }
    }
    [[nodiscard]] auto snapshot() const
    {
        std::vector<std::pair<Key, T>> v;
        for (auto &s : shards_)
        {
            std::shared_lock<std::shared_mutex> l(s.mutex);
            for (auto &x : s.data)
                v.emplace_back(x.first, x.second);
        }
        return v;
    }
};
} // namespace cstl

#ifdef _MSC_VER
#pragma warning(pop)
#endif
