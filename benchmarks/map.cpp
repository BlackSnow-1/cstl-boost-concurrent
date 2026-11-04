// SPDX-License-Identifier: MIT
#include <cstl/concurrent_unordered_map.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

template <class Work>
double measure(int threads, int operations, Work work) {
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            ready.fetch_add(1);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int i = 0; i < operations; ++i) work(t, i);
        });
    }
    while (ready.load() < threads) std::this_thread::yield();
    auto begin = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
}

int main(int argc, char** argv) {
    const int threads = argc > 1 ? std::atoi(argv[1]) : 4;
    const int operations = argc > 2 ? std::atoi(argv[2]) : 100000;
    if (threads < 1 || threads > 256 || operations < 1 || operations > 10000000) return 2;
    constexpr int keys = 65536;
    cstl::concurrent_unordered_map<int, std::uint64_t> concurrent(keys);
    std::unordered_map<int, std::uint64_t> standard;
    standard.reserve(keys);
    for (int k = 0; k < keys; ++k) { concurrent.try_emplace(k, 0); standard.emplace(k, 0); }
    std::mutex lock;
    auto key = [](int t, int i) {
        return static_cast<int>((static_cast<std::uint32_t>(i) * 2654435761u +
            static_cast<std::uint32_t>(t) * 1013904223u) & (keys - 1));
    };
    const double c = measure(threads, operations, [&](int t, int i) {
        concurrent.visit(key(t, i), [](auto& n) { ++n; });
    });
    const double s = measure(threads, operations, [&](int t, int i) {
        std::lock_guard<std::mutex> guard(lock);
        ++standard.find(key(t, i))->second;
    });
    std::uint64_t sum_c = 0, sum_s = 0;
    for (const auto& entry : concurrent.snapshot()) sum_c += entry.second;
    for (const auto& entry : standard) sum_s += entry.second;
    if (sum_c != static_cast<std::uint64_t>(threads) * operations || sum_c != sum_s) return 1;
    const double total = static_cast<double>(threads) * operations;
    std::cout << "threads=" << threads << ", ops/thread=" << operations << ", keys=" << keys
              << ", workload=prepopulated integer map updates\n"
              << "cstl: " << total / c / 1e6 << " Mops/s\n"
              << "std::unordered_map + mutex: " << total / s / 1e6 << " Mops/s\n"
              << "Validation: both totals=" << sum_c
              << ". This workload is not a general performance ranking.\n";
}
