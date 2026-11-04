// SPDX-License-Identifier: MIT
#pragma once
#include <boost/config.hpp>
#include <functional>
#include <type_traits>
#include <utility>
namespace cstl::detail {
// Keep copyable temporary callables inline in Boost's visitor. Refer to lvalue
// and move-only callables, preserving their state and avoiding extra copies.
template<class F> struct map_value_visitor {
    using callable_type=std::remove_reference_t<F>;
    using storage_type=std::conditional_t<std::is_lvalue_reference_v<F> ||
        !std::is_copy_constructible_v<callable_type>,F&&,callable_type>;
    storage_type callback;
    template<class Entry>
    BOOST_FORCEINLINE decltype(auto) operator()(Entry&& entry) {
        return std::invoke(std::forward<F>(callback),std::forward<Entry>(entry).second);
    }
};
} // namespace cstl::detail
