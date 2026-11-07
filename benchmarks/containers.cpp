// SPDX-License-Identifier: MIT
#include <cstl/cstl.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using Value = std::uint64_t;
using Map = cstl::concurrent_unordered_map<Value, Value>;
using Set = cstl::concurrent_unordered_set<Value>;
using Blocking = cstl::blocking_queue<Value>;
using Concurrent = cstl::concurrent_queue<Value>;
using Bounded = cstl::bounded_queue<Value>;
using Spsc = cstl::spsc_queue<Value>;
using Priority = cstl::concurrent_priority_queue<Value>;

constexpr std::size_t key_count = 65536;
constexpr std::size_t queue_capacity = 1024;
constexpr std::size_t hash_operations = 2000000;
constexpr std::size_t messages = 400000;
constexpr unsigned repeats = 7;
constexpr auto wait_limit = std::chrono::seconds(10);

template <class Work>
double run_workers(unsigned count, Work work) {
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(count);
    for (unsigned index = 0; index < count; ++index) {
        threads.emplace_back([&, index] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {}
            work(index);
        });
    }
    while (ready.load(std::memory_order_acquire) != count) {}
    const auto begin = Clock::now();
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }
    return std::chrono::duration<double>(Clock::now() - begin).count();
}

template <class Table, bool Write>
double run_hash(unsigned threads, std::size_t operations) {
    Table table(key_count);
    for (Value key = 0; key < key_count; ++key) {
        if constexpr (std::is_same_v<Table, Map>) {
            table.try_emplace(key, 0);
        } else {
            table.insert(key);
        }
    }
    const auto per_thread = operations / threads;
    const auto keys_per_thread = key_count / threads;
    std::vector<std::size_t> successes(threads);
    const double seconds = run_workers(threads, [&](unsigned index) {
        std::size_t ok = 0;
        for (std::size_t i = 0; i < per_thread; ++i) {
            const Value step = Value{i} * UINT64_C(2654435761);
            if constexpr (!Write) {
                const auto key = (step + Value{index} * UINT64_C(1013904223)) % key_count;
                ok += table.contains(key);
            } else if constexpr (std::is_same_v<Table, Map>) {
                const auto key = (step + Value{index} * UINT64_C(1013904223)) % key_count;
                ok += table.visit(key, [](Value& value) { ++value; });
            } else {
                // Each worker owns a key range so erase/reinsert pairs cannot overlap.
                const auto key = index * keys_per_thread + step % keys_per_thread;
                ok += table.erase(key) == 1 && table.insert(key);
            }
        }
        successes[index] = ok;
    });
    if (table.size() != key_count ||
        !std::all_of(successes.begin(), successes.end(),
                     [=](std::size_t ok) { return ok == per_thread; })) {
        throw std::runtime_error("hash workload lost a key or operation");
    }
    if constexpr (Write && std::is_same_v<Table, Map>) {
        Value total = 0;
        for (const auto& entry : table.snapshot()) {
            total += entry.second;
        }
        if (total != operations) {
            throw std::runtime_error("map workload lost an update");
        }
    }
    return seconds;
}

template <class Try>
bool retry_until(Clock::time_point deadline, const std::atomic<bool>& failed, Try attempt) {
    for (unsigned misses = 0; !attempt(); ++misses) {
        if ((misses & 255) == 255 &&
            (failed.load(std::memory_order_relaxed) || Clock::now() >= deadline)) {
            return false;
        }
    }
    return true;
}

template <class Queue>
double run_queue(unsigned pairs, std::size_t count) {
    Queue queue(queue_capacity);
    const auto per_worker = count / pairs;
    std::vector<Value> received(count);
    std::atomic<bool> failed{false};
    const double seconds = run_workers(2 * pairs, [&](unsigned index) {
        const auto deadline = Clock::now() + wait_limit;
        for (std::size_t i = 0; i < per_worker; ++i) {
            bool ok = true;
            if (index < pairs) {
                const Value value = index * per_worker + i;
                if constexpr (std::is_same_v<Queue, Priority>) {
                    queue.push(value);
                } else if constexpr (std::is_same_v<Queue, Concurrent> ||
                                     std::is_same_v<Queue, Blocking>) {
                    ok = queue.push(value);
                } else {
                    ok = retry_until(deadline, failed, [&] { return queue.try_push(value); });
                }
            } else {
                auto& value = received[(index - pairs) * per_worker + i];
                if constexpr (std::is_same_v<Queue, Blocking>) {
                    ok = queue.wait_pop_for(value, wait_limit);
                } else {
                    ok = retry_until(deadline, failed, [&] { return queue.try_pop(value); });
                }
            }
            if (!ok) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    });
    if (failed.load(std::memory_order_relaxed)) {
        throw std::runtime_error("queue transfer timed out or push failed");
    }
    std::vector<bool> seen(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = received[i];
        if (value >= count || seen[value] ||
            (std::is_same_v<Queue, Spsc> && value != i)) {
            throw std::runtime_error("queue returned an invalid, repeated, or out-of-order message");
        }
        seen[value] = true;
    }
    return seconds;
}

using Measure = double (*)(unsigned, std::size_t);
struct Scenario {
    std::string_view container;
    std::string_view workload;
    unsigned workers;
    std::size_t operations;
    Measure measure;
};

const Scenario scenarios[] = {
    {"concurrent_unordered_map", "contains", 1, hash_operations, run_hash<Map, false>},
    {"concurrent_unordered_map", "contains", 4, hash_operations, run_hash<Map, false>},
    {"concurrent_unordered_map", "update", 4, hash_operations, run_hash<Map, true>},
    {"concurrent_unordered_set", "contains", 1, hash_operations, run_hash<Set, false>},
    {"concurrent_unordered_set", "contains", 4, hash_operations, run_hash<Set, false>},
    {"concurrent_unordered_set", "erase_reinsert", 4, hash_operations, run_hash<Set, true>},
    {"spsc_queue", "handoff", 1, messages, run_queue<Spsc>},
    {"bounded_queue", "handoff", 1, messages, run_queue<Bounded>},
    {"bounded_queue", "handoff", 2, messages, run_queue<Bounded>},
    {"concurrent_queue", "handoff", 1, messages, run_queue<Concurrent>},
    {"concurrent_queue", "handoff", 2, messages, run_queue<Concurrent>},
    {"blocking_queue", "handoff", 1, messages, run_queue<Blocking>},
    {"blocking_queue", "handoff", 2, messages, run_queue<Blocking>},
    {"concurrent_priority_queue", "handoff", 1, messages, run_queue<Priority>},
    {"concurrent_priority_queue", "handoff", 2, messages, run_queue<Priority>},
};
}

int main() {
    try {
        std::cout << "container,workload,workers_or_pairs,round,operations,seconds,millions_per_second\n";
        for (const auto& scenario : scenarios) {
            scenario.measure(scenario.workers, scenario.operations / 10);
        }
        for (unsigned round = 0; round < repeats; ++round) {
            for (std::size_t i = 0; i < std::size(scenarios); ++i) {
                const auto& scenario = scenarios[(i + round) % std::size(scenarios)];
                const auto seconds = scenario.measure(scenario.workers, scenario.operations);
                std::cout << scenario.container << ',' << scenario.workload << ','
                          << scenario.workers << ',' << round + 1 << ','
                          << scenario.operations << ',' << seconds << ','
                          << scenario.operations / seconds / 1e6 << '\n';
            }
            std::cout.flush();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
