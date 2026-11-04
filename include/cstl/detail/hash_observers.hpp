// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

namespace cstl::detail {

// Upstream's parallel size()/capacity() read shard metadata without locking.
// Observe each shard under its own lock instead. This is not a global snapshot.
template <class Table>
std::size_t locked_size(const Table& table) {
    std::size_t result = 0;
    for (std::size_t i = 0; i < Table::subcnt(); ++i) {
        table.with_submap(i, [&](const auto& shard) { result += shard.size(); });
    }
    return result;
}

// Avoid upstream parallel reserve(), whose capacity() precheck is unlocked.
template <class Table>
void locked_reserve(Table& table, std::size_t count) {
    const auto shards = Table::subcnt();
    const auto per_shard = count / shards + (count % shards != 0);
    for (std::size_t i = 0; i < shards; ++i) {
        table.with_submap_m(i, [&](auto& shard) { shard.reserve(per_shard); });
    }
}

} // namespace cstl::detail
