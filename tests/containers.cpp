// SPDX-License-Identifier: MIT
#include <cstl/cstl.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " << #expr << '\n'; \
    std::abort(); } } while (false)

using namespace std::chrono_literals;

void basic_map_set() {
    cstl::concurrent_unordered_map<std::string, int> m;
    CHECK(m.empty());
    CHECK(m.insert({"a", 1}));
    CHECK(!m.insert({"a", 3}));
    CHECK(m.find("a") == 1);
    CHECK(!m.find("missing"));
    CHECK(!m.insert_or_assign("a", 2));
    CHECK(m.insert_or_assign("b", 4));
    CHECK(m.visit("a", [](int& x) { x += 5; }));
    CHECK(m.find("a") == 7);
    CHECK(!m.visit("missing", [](int&) { CHECK(false); }));
    CHECK(m.try_emplace_or_visit("c", [](int&) { CHECK(false); }, 10));
    CHECK(!m.try_emplace_or_visit("c", [](int& x) { ++x; }, 100));
    CHECK(m.find("c") == 11);
    CHECK(!m.erase_if("c", [](int x) { return x == 12; }));
    CHECK(m.erase_if("c", [](int x) { return x == 11; }));
    try { m.visit("a", [](int&) { throw std::runtime_error("callback"); }); }
    catch (const std::runtime_error&) {}
    CHECK(m.find("a") == 7); // throwing visitor must release its lock
    CHECK(m.snapshot().size() == 2);
    CHECK(m.erase("a") == 1);
    CHECK(m.erase("a") == 0);
    m.clear();
    CHECK(m.empty());

    cstl::concurrent_unordered_map<int, std::unique_ptr<int>> movable;
    CHECK(movable.try_emplace(1, std::make_unique<int>(8)));
    auto unused = std::make_unique<int>(99);
    CHECK(!movable.try_emplace(1, std::move(unused)));
    CHECK(unused && *unused == 99); // duplicate does not consume the argument
    CHECK(movable.cvisit(1, [](const auto& p) { CHECK(*p == 8); }));

    cstl::concurrent_unordered_set<std::string> s;
    CHECK(s.emplace("hello"));
    CHECK(!s.insert("hello"));
    CHECK(s.contains("hello"));
    CHECK(s.snapshot().size() == 1);
    CHECK(s.erase("hello") == 1);
    CHECK(s.empty());
    s.reserve(100);
    CHECK(s.insert("again"));
    s.clear();
    CHECK(s.empty());
}

struct ConstantHash { std::size_t operator()(int) const noexcept { return 0; } };

void collisions() {
    cstl::concurrent_unordered_map<int, int, ConstantHash, std::equal_to<int>, 0> m;
    for (int i = 0; i < 1000; ++i) CHECK(m.try_emplace(i, i));
    for (int i = 0; i < 1000; i += 2) CHECK(m.erase(i) == 1);
    for (int i = 1; i < 1000; i += 2) CHECK(m.find(i) == i);
    CHECK(m.size() == 500);
}

void shared_counter() {
    cstl::concurrent_unordered_map<int, int> m;
    constexpr int threads = 6, iterations = 15000, keys = 64;
    std::atomic<bool> start{false}, done{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int i = 0; i < iterations; ++i)
                m.try_emplace_or_visit(i % keys, [](int& x) { ++x; }, 1);
        });
    }
    std::thread observer([&] {
        while (!done.load(std::memory_order_acquire)) {
            CHECK(m.size() <= keys);
            auto copy = m.snapshot();
            for (const auto& item : copy) CHECK(item.second > 0);
            m.reserve(512); // concurrent observation + growth must be safe
        }
    });
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    done.store(true, std::memory_order_release);
    observer.join();
    CHECK(m.size() == keys);
    int sum = 0;
    for (const auto& item : m.snapshot()) sum += item.second;
    CHECK(sum == threads * iterations);
}

struct ConsistentValue { int a; int b; };

void growth_erase_and_read() {
    cstl::concurrent_unordered_map<int, ConsistentValue> m;
    cstl::concurrent_unordered_set<int> s;
    constexpr int threads = 4, count = 6000;
    std::atomic<bool> done{false};
    std::vector<std::thread> writers;
    for (int t = 0; t < threads; ++t) {
        writers.emplace_back([&, t] {
            for (int i = 0; i < count; ++i) {
                int key = t * count + i;
                CHECK(m.try_emplace(key, ConsistentValue{key, -key}));
                CHECK(s.insert(key));
                if (i % 2 == 0) { CHECK(m.erase(key) == 1); CHECK(s.erase(key) == 1); }
                const int shared = -(i % 32) - 1;
                m.insert_or_assign(shared, ConsistentValue{i, -i});
                if (i % 3 == 0) m.erase(shared);
            }
        });
    }
    std::thread reader([&] {
        int n = 0;
        while (!done.load(std::memory_order_acquire)) {
            const auto key = -(n++ % 32) - 1;
            m.cvisit(key, [](const ConsistentValue& v) { CHECK(v.a == -v.b); });
            auto copy = m.find(key);
            if (copy) CHECK(copy->a == -copy->b);
            (void)s.contains(n % (threads * count));
            (void)s.size();
            if (n % 200 == 0) { m.reserve(threads * count); s.reserve(threads * count); }
        }
    });
    for (auto& writer : writers) writer.join();
    done.store(true, std::memory_order_release);
    reader.join();
    for (int key = -32; key < 0; ++key) m.erase(key);
    CHECK(m.size() == threads * count / 2);
    CHECK(s.size() == threads * count / 2);
    for (int key = 0; key < threads * count; ++key) {
        CHECK(m.contains(key) == (key % 2 != 0));
        CHECK(s.contains(key) == (key % 2 != 0));
    }
}

void basic_queues() {
    cstl::concurrent_queue<int> q;
    int out = -1;
    CHECK(!q.try_pop(out));
    auto producer = q.make_producer_token();
    auto consumer = q.make_consumer_token();
    for (int i = 0; i < 1000; ++i) CHECK(q.push(producer, i));
    for (int i = 0; i < 1000; ++i) { CHECK(q.try_pop(consumer, out)); CHECK(out == i); }
    CHECK(q.size_approx() == 0);
    CHECK(q.try_push(producer, 7));
    CHECK(q.try_pop(consumer, out) && out == 7);
    std::vector<int> input{1, 2, 3, 4}, output(4);
    CHECK(q.push_bulk(input.begin(), input.size()));
    CHECK(q.try_pop_bulk(output.begin(), output.size()) == input.size());
    CHECK(input == output);
    cstl::concurrent_queue<std::unique_ptr<int>> movable;
    CHECK(movable.push(std::make_unique<int>(42)));
    std::unique_ptr<int> pointer;
    CHECK(movable.try_pop(pointer) && *pointer == 42);
}

void queue_handoff() {
    constexpr int producers = 4, consumers = 4, per_producer = 20000;
    constexpr int total = producers * per_producer;
    cstl::concurrent_queue<int> q(1024);
    std::vector<std::atomic<int>> seen(total);
    for (auto& x : seen) x.store(0, std::memory_order_relaxed);
    std::atomic<int> received{0};
    std::vector<std::thread> workers;
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    for (int t = 0; t < consumers; ++t) {
        workers.emplace_back([&] {
            auto token = q.make_consumer_token();
            while (received.load(std::memory_order_acquire) < total) {
                int value;
                if (q.try_pop(token, value)) {
                    CHECK(value >= 0 && value < total);
                    CHECK(seen[value].fetch_add(1, std::memory_order_relaxed) == 0);
                    received.fetch_add(1, std::memory_order_release);
                } else {
                    CHECK(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::yield();
                }
            }
        });
    }
    for (int t = 0; t < producers; ++t) {
        workers.emplace_back([&, t] {
            auto token = q.make_producer_token();
            for (int i = 0; i < per_producer; ++i) CHECK(q.push(token, t * per_producer + i));
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& x : seen) CHECK(x.load(std::memory_order_relaxed) == 1);
    CHECK(received.load() == total);
}

void blocking_handoff() {
    cstl::blocking_queue<int> q;
    int out = -1;
    CHECK(!q.wait_pop_for(out, -1ms)); // must not become an infinite wait
    CHECK(!q.wait_pop_for(out, 0ms));
    CHECK(!q.wait_pop_for(out, 2ms));
    std::thread producer([&] { CHECK(q.push(42)); });
    CHECK(q.wait_pop_for(out, 2s) && out == 42);
    producer.join();
    CHECK(q.push(7));
    q.wait_pop(out);
    CHECK(out == 7);
    constexpr int total = 20000, consumers = 3;
    std::atomic<int> received{0};
    std::vector<std::thread> workers;
    for (int t = 0; t < consumers; ++t) {
        workers.emplace_back([&] {
            for (;;) {
                int value;
                CHECK(q.wait_pop_for(value, 5s));
                if (value == -1) break;
                CHECK(value >= 0 && value < total);
                received.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    // One producer keeps payload-before-sentinel ordering for this shutdown.
    for (int i = 0; i < total; ++i) CHECK(q.push(i));
    for (int t = 0; t < consumers; ++t) CHECK(q.push(-1));
    for (auto& worker : workers) worker.join();
    CHECK(received.load() == total);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // Test intentionally over-aligned payloads.
#endif
struct alignas(128) AlignedPayload {
    int value = 0;
    AlignedPayload() = default;
    explicit AlignedPayload(int n) noexcept : value(n) {}
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

void spsc_handoff() {
    cstl::spsc_queue<int> small(3);
    CHECK(small.capacity() == 3);
    CHECK(small.try_push(1)); CHECK(small.try_push(2)); CHECK(small.try_push(3));
    CHECK(!small.try_push(4));
    int out;
    for (int i = 1; i <= 3; ++i) { CHECK(small.try_pop(out)); CHECK(out == i); }
    CHECK(!small.try_pop(out));
    bool rejected = false;
    try { cstl::spsc_queue<int> invalid(0); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    cstl::spsc_queue<AlignedPayload> aligned(7);
    CHECK(aligned.try_emplace(42));
    AlignedPayload payload;
    CHECK(aligned.try_pop(payload) && payload.value == 42);
    cstl::spsc_queue<std::unique_ptr<int>> movable(2);
    CHECK(movable.try_push(std::make_unique<int>(9)));
    std::unique_ptr<int> pointer;
    CHECK(movable.try_pop(pointer) && *pointer == 9);

    cstl::spsc_queue<int> q(127);
    constexpr int total = 150000;
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (int i = 0; i < total; ++i) {
            while (!q.try_push(i)) { CHECK(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
        }
    });
    std::thread consumer([&] {
        for (int i = 0; i < total; ++i) {
            int value;
            while (!q.try_pop(value)) { CHECK(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
            CHECK(value == i);
        }
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire)) { CHECK(q.size_approx() <= q.capacity()); std::this_thread::yield(); }
    producer.join(); consumer.join();
    CHECK(q.size_approx() == 0);
}

void bounded_handoff() {
    bool rejected = false;
    try { cstl::bounded_queue<int> invalid(3); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    cstl::bounded_queue<int> small(2);
    CHECK(small.try_push(1)); CHECK(small.try_push(2)); CHECK(!small.try_push(3));
    int value;
    CHECK(small.try_pop(value) && value == 1);
    CHECK(small.try_push(3));
    CHECK(small.try_pop(value) && value == 2);
    CHECK(small.try_pop(value) && value == 3);
    CHECK(!small.try_pop(value));
    cstl::bounded_queue<std::unique_ptr<int>> movable(2);
    CHECK(movable.try_push(std::make_unique<int>(9)));
    std::unique_ptr<int> pointer;
    CHECK(movable.try_pop(pointer) && *pointer == 9);

    constexpr int producers = 4, consumers = 4, per_producer = 15000, total = producers * per_producer;
    cstl::bounded_queue<int> q(256);
    std::vector<std::atomic<int>> seen(total);
    for (auto& x : seen) x.store(0);
    std::atomic<int> received{0};
    std::vector<std::thread> workers;
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    for (int t = 0; t < consumers; ++t) {
        workers.emplace_back([&] {
            while (received.load() < total) {
                int item;
                if (q.try_pop(item)) {
                    CHECK(item >= 0 && item < total);
                    CHECK(seen[item].fetch_add(1) == 0);
                    received.fetch_add(1);
                } else {
                    CHECK(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::yield();
                }
            }
        });
    }
    for (int t = 0; t < producers; ++t) {
        workers.emplace_back([&, t] {
            for (int i = 0; i < per_producer; ++i) {
                while (!q.try_push(t * per_producer + i)) {
                    CHECK(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& x : seen) CHECK(x.load() == 1);
}

void priority_handoff() {
    constexpr int producers = 4, per_producer = 5000, total = producers * per_producer;
    cstl::concurrent_priority_queue<int> q(128);
    std::vector<std::thread> workers;
    for (int t = 0; t < producers; ++t) {
        workers.emplace_back([&, t] { for (int i = 0; i < per_producer; ++i) q.push(t * per_producer + i); });
    }
    for (auto& worker : workers) worker.join();
    CHECK(q.size_approx() == total);
    for (int expected = total - 1; expected >= 0; --expected) {
        int value;
        CHECK(q.try_pop(value) && value == expected);
    }
    int out;
    CHECK(!q.try_pop(out));
    cstl::concurrent_priority_queue<int, std::greater<>> minimum;
    minimum.push(3); minimum.push(1); minimum.push(2);
    CHECK(minimum.try_pop(out) && out == 1);
    minimum.clear_unsafe();
    CHECK(minimum.size_approx() == 0);

    std::vector<std::atomic<int>> seen(total);
    for (auto& x : seen) x.store(0);
    std::atomic<int> received{0};
    workers.clear();
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    for (int t = 0; t < 3; ++t) {
        workers.emplace_back([&] {
            while (received.load() < total) {
                int item;
                if (q.try_pop(item)) {
                    CHECK(item >= 0 && item < total);
                    CHECK(seen[item].fetch_add(1) == 0);
                    received.fetch_add(1);
                } else {
                    CHECK(std::chrono::steady_clock::now() < deadline);
                    std::this_thread::yield();
                }
            }
        });
    }
    for (int t = 0; t < producers; ++t) {
        workers.emplace_back([&, t] { for (int i = 0; i < per_producer; ++i) q.push(t * per_producer + i); });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& x : seen) CHECK(x.load() == 1);
    CHECK(q.size_approx() == 0);
}

struct Tracked {
    inline static std::atomic<int> live{0};
    int value = 0;
    Tracked() noexcept { ++live; }
    explicit Tracked(int n) noexcept : value(n) { ++live; }
    Tracked(const Tracked& other) noexcept : value(other.value) { ++live; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++live; }
    Tracked& operator=(Tracked&& other) noexcept { value = other.value; return *this; }
    Tracked& operator=(const Tracked& other) noexcept { value = other.value; return *this; }
    ~Tracked() noexcept { --live; }
    friend bool operator<(const Tracked& a, const Tracked& b) noexcept { return a.value < b.value; }
};

struct ThrowOnConstruction {
    int value = 0;
    ThrowOnConstruction() = default;
    explicit ThrowOnConstruction(int n) : value(n) { if (n < 0) throw std::runtime_error("payload"); }
    ThrowOnConstruction& operator=(ThrowOnConstruction&&) noexcept = default;
};

void payload_lifetimes() {
    CHECK(Tracked::live.load() == 0);
    {
        cstl::spsc_queue<Tracked> q(3);
        CHECK(q.try_emplace(1)); CHECK(q.try_emplace(2));
        Tracked out;
        CHECK(q.try_pop(out) && out.value == 1);
        CHECK(q.try_emplace(3)); // destruction must clean up unread elements
    }
    CHECK(Tracked::live.load() == 0);
    {
        cstl::bounded_queue<Tracked> q(2);
        CHECK(q.try_emplace(1)); CHECK(q.try_emplace(2));
        Tracked out;
        CHECK(q.try_pop(out) && out.value == 1);
        CHECK(q.try_emplace(3)); // reuse a released ring slot
    }
    CHECK(Tracked::live.load() == 0);
    {
        cstl::concurrent_priority_queue<Tracked> q;
        q.emplace(1); q.emplace(3); q.emplace(2);
        Tracked out;
        CHECK(q.try_pop(out) && out.value == 3);
    }
    CHECK(Tracked::live.load() == 0);
    {
        cstl::concurrent_queue<Tracked> q;
        CHECK(q.push(Tracked(1))); CHECK(q.push(Tracked(2)));
    }
    CHECK(Tracked::live.load() == 0);
    {
        cstl::blocking_queue<Tracked> q;
        CHECK(q.push(Tracked(1)));
    }
    CHECK(Tracked::live.load() == 0);
    {
        cstl::spsc_queue<ThrowOnConstruction> q(2);
        bool thrown = false;
        try { (void)q.try_emplace(-1); } catch (const std::runtime_error&) { thrown = true; }
        CHECK(thrown);
        CHECK(q.try_emplace(7));
        ThrowOnConstruction out;
        CHECK(q.try_pop(out) && out.value == 7);
        CHECK(!q.try_pop(out));
    }
}

int main() {
    basic_map_set();
    collisions();
    shared_counter();
    growth_erase_and_read();
    basic_queues();
    queue_handoff();
    blocking_handoff();
    spsc_handoff();
    bounded_handoff();
    priority_handoff();
    payload_lifetimes();
    std::cout << "PASS: 11 suites; map/set growth, erase, observation, hot keys; "
                 "MPMC 80,000 + bounded 60,000 + SPSC 150,000 messages; "
                 "priority ordering and concurrent handoff; blocking waits\n";
}
