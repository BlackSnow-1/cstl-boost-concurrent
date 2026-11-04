// SPDX-License-Identifier: MIT
#include <cstl/cstl.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
    cstl::concurrent_unordered_map<std::string, int> counts;
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < 10000; ++i)
                counts.try_emplace_or_visit("requests", [](int& n) { ++n; }, 1);
        });
    }
    for (auto& worker : workers) worker.join();
    std::cout << "requests=" << counts.find("requests").value_or(0) << '\n';

    cstl::blocking_queue<std::string> jobs;
    std::thread consumer([&] {
        std::string job;
        jobs.wait_pop(job);
        std::cout << "job=" << job << '\n';
    });
    if (!jobs.push("hello")) {
        // Do not leave the consumer waiting forever after an enqueue failure.
        std::terminate();
    }
    consumer.join();
}
