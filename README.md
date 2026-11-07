# cstl-boost-concurrent

源码自包含的 C++17 并发容器库。

CSTL 1.0.0.0 面向**通用并发读写，兼顾吞吐和易用性**。它从成熟项目中选取适合的数据结构，把必要源码和许可证一起带入，提供统一的 `cstl::` 接口。

**使用时无需安装、链接或下载 Boost、oneTBB、Folly、libcds、parallel-hashmap 或 ConcurrentQueue。** 需要 C++17 标准库和系统线程支持；阻塞队列还使用系统信号量。构建过程没有联网步骤。本正式版的功能和性能验证范围见 [验证记录](VALIDATION.md)。

首个正式发布为 **1.0.0.0**，CMake 包版本与 GitHub Release 版本一致。C++ 容器实现与已验证的 0.3.0 保持逐字节一致，原有性能记录继续适用。发布内容见 [发布说明](RELEASE_NOTES.md)。

“STL 风格”指命名、模板和使用习惯。并发访问采用 `visit`、值副本和 `try_pop`，没有把容易产生悬空引用的 `operator[]`、`front()`、`top()` 或普通迭代器暴露出来。

## 已支持的容器

| 类型 | 选用的核心实现 | 本版保留的优势 |
|---|---|---|
| `concurrent_unordered_map<Key,T,Hash,Equal,ShardBits>` | Boost 1.87.0 完整 `unordered_flat_map`，分片读写锁 | 一次哈希、分片扩容、锁内复合更新；增长与平凡元素搬迁经过定制 |
| `concurrent_unordered_set<Key,Hash,Equal,ShardBits>` | parallel-hashmap 的分片 flat hash set | 分片并发、查重与删除、紧凑存储 |
| `concurrent_queue<T>` | moodycamel ConcurrentQueue | MPMC、生产者分流、显式 token、批量入队/出队 |
| `blocking_queue<T>` | moodycamel BlockingConcurrentQueue | MPMC、信号量等待、限时等待，避免空队列持续轮询 |
| `spsc_queue<T>` | Folly ProducerConsumerQueue 的独立移植 | SPSC、固定容量、游标缓存、缓存行隔离 |
| `bounded_queue<T>` | libcds VyukovMPMCCycleQueue 的核心循环移植 | 有界 MPMC、预分配环形存储、操作期间不增加队列存储分配 |
| `concurrent_priority_queue<T,Compare>` | oneTBB 优先队列与 aggregator 的独立移植 | 多线程提交操作、flat combining 批处理、二叉堆优先级 |

本版没有有序 Map/Set、并发 Vector/List/Deque、独立 MPSC 优化队列或提供严格全局 FIFO 的通用无界队列。MPMC 容器可用于 MPSC/SPSC 场景，但不代表采用了对应的专用优化。

选择的是当前候选中**适合本库目标的默认实现**，不是声称这些实现对所有机器、键类型和负载都最快。尚未完成与全部上游实现的统一性能对比，详见 [选型依据](SELECTION.md) 和 [验证记录](VALIDATION.md)。

## 队列支持与使用边界

MPMC = 多生产者、多消费者；SPSC = 单生产者、单消费者。

| 队列 | 并发模型 | 顺序语义 | 容量/内存 | 满或空时 | 进度保证 |
|---|---|---|---|---|---|
| `concurrent_queue` | MPMC | 保留单个生产者的顺序；不保证跨生产者全局 FIFO | 可增长；构造参数是初始预分配量，不是硬上限 | `push` 返回成功/失败；`try_pop` 返回是否取得元素 | 上游核心为 lock-free 算法；分配器和用户类型操作不因此变为无阻塞 |
| `blocking_queue` | MPMC | 同上 | 可增长；系统信号量 | `wait_pop` 等待；`wait_pop_for` 等待至成功或超时 | 明确会阻塞；不保证等待者公平性 |
| `spsc_queue` | **恰好一个生产者和一个消费者** | FIFO；多消费者不存在于其使用契约中 | 固定逻辑容量；底层槽数按二次幂分配 | `try_push/try_emplace` 满则失败，`try_pop` 空则失败 | 索引算法为 wait-free；用户构造/赋值不包含在此保证内 |
| `bounded_queue` | MPMC | 保留环形队列的槽位顺序/因果 FIFO | 固定容量，必须是 ≥2 的二次幂 | 满/空返回失败；有未完成槽位操作时也可能自旋等待 | **不是严格意义的 lock-free 或 wait-free**；暂停的槽位持有者可能阻碍其他线程 |
| `concurrent_priority_queue` | MPMC | 默认最大值优先；相同优先级不保证稳定顺序 | 可增长，底层 `std::vector` | `try_pop` 空则失败；`push/emplace` 通过异常报告失败 | flat combining 会串行执行一个批次，其他提交者可能等待 |

### 所有队列的共同约束

1. 先构造队列，再安全地发布给工作线程。**销毁前必须让所有生产者、消费者、观察者退出并完成 `join()`**；析构不是取消正在进行的操作。
2. 每次出队以 `try_pop(out)` 的返回值判断是否取得元素。不要先判断大小，再假定下一步一定能取到元素。
3. `size_approx()` 仅用于监控和启发式调度。它不是内存同步点，也不能用于判断“全部工作完成”“可以销毁”或“最后一条消息已经消费”。`bounded_queue` 故意不提供大小接口。
4. 队列传递的是 `T` 对象。若 `T` 是指针、`shared_ptr` 或包含共享外部对象，队列不会自动保护所指向对象后续的读写。
5. 多消费者的**处理完成顺序**可以与出队顺序不同。需要业务完成顺序时，应额外使用序号和排序/提交机制。
6. 容器均不可复制或移动，避免并发期间迁移存储。头文件中的 `detail` 实现不属于稳定公开接口。

### `concurrent_queue` 的特别约束

- 每次检查 `push`、`try_push`、`push_bulk` 的 `bool` 返回值；忽略失败会丢业务消息。
- `push` 可增加队列存储。`try_push` 不增加队列块分配，但**不带 token 的第一次隐式生产者注册仍可能分配**。显式 token 的创建也可能分配，应在热点路径之前完成。
- `try_push` 失败不等同于一个精确的全局“满”状态；它受生产者子队列及预分配块可用性影响。要求确定内存上限时，使用 `bounded_queue`。
- producer/consumer token 必须来自当前队列；**同一个 token 不可被多个线程同时使用**，并且必须在队列销毁前释放。不同消费者各用自己的 consumer token。
- 跨生产者的同步关系不产生全局 FIFO 保证；严格跨生产者排序需求不能直接交给这个队列。
- `try_pop_bulk` 的输出范围必须可写且至少容纳 `max_count` 个元素；返回值为实际出队数。需要移动批量输入时传入 `std::make_move_iterator`。

### `blocking_queue` 的特别约束

- 没有 `close()`、自动取消、背压或停止令牌；`wait_pop` 在没有新元素时可以永久等待。
- 需要可退出的工作线程时，可使用 `wait_pop_for` 搭配由业务维护的停止协议。超时会受到系统调度和时钟精度影响，不是硬实时期限。
- `wait_pop_for(out, timeout)` 的零或负时长被归一为立即尝试；不会因底层负数约定意外变成无限等待。传入时长必须能够表示为有符号 64 位微秒。
- 不可在任意生产者尚有消息未完成发布时随意发送终止哨兵。跨生产者没有全局 FIFO，哨兵可能先被看到。
- 本库测试中的哨兵示例仅用于**同一个生产者先发完数据、再发每位消费者的哨兵**，并在所有消费者结束后销毁队列。复杂 MPMC 停机请使用独立的生产者完成/业务在途计数协议。

### `spsc_queue` 的特别约束

- 多个生产者并发调用写接口，或多个消费者并发调用读接口，违反契约；不要用一次测试没出错作为支持依据。
- 容量参数范围为 `[1, 2^31]`；`capacity()` 返回可用逻辑容量。内部分配向上取整至二次幂，内存预算按底层槽数估算。
- 本版要求目标平台支持始终 lock-free 的 64 位原子，否则编译失败；要求 `T` 的移动赋值和析构为 `noexcept`。
- 元素构造可抛异常；构造失败时不发布写游标。为了稳定延迟，推荐使用不分配、不抛异常的轻量消息类型。
- `size_approx` 可从第三个观察线程调用，结果夹在 `[0, capacity()]` 范围内，但它不提供同步保证。
- 游标使用 64 位递增计数；不承诺经历完整 `2^64` 次操作回绕后的行为。

### `bounded_queue` 的特别约束

- 构造时校验容量。入队构造以及出队移动赋值必须 `noexcept`；在取得槽位后抛异常会破坏发布协议，因此这些约束由 `static_assert` 强制。
- “操作期间不分配队列存储”不意味着消息类型自身的操作不分配内存。`std::string` 的复制通常不满足不抛异常构造要求，可改为移动或使用轻量句柄。
- `try_` 表示会在满/空时返回失败，**不表示等待时间有固定上界**。竞争、线程暂停及槽位发布会导致自旋；不适合硬实时截止时间。
- 没有按时间等待、自动增长、关闭或丢弃最旧消息功能。

### `concurrent_priority_queue` 的特别约束

- 默认 `std::less<>` 构成最大优先队列；最小优先队列使用 `std::greater<>`。比较器必须提供严格弱序且不抛异常。
- 要求 `T` 的移动构造、移动赋值和析构均为 `noexcept`，避免处理线程抛出异常后使其他提交者永久等待；编译期检查这些要求。
- `push` 返回 `void`。继承所选上游实现的失败处理：批次中插入失败会在调用方表现为 `std::bad_alloc`，不保留所有原始异常类型。
- 并发期间只允许使用公开的 `push`、`emplace`、`try_pop`、`size_approx`。`clear_unsafe()` 必须在所有其他访问停止后调用。
- 没有 `top()` 引用，也没有“查看最高优先级后稍后仍保证弹出同一元素”的契约。持续高优先级流量可能使低优先级任务长期得不到处理。
- 自包含移植替换了 oneTBB 的内存分配器、等待策略并移除了 profiling；因此不直接继承 oneTBB 的性能数据或全部验证结论。

## 哈希容器的使用边界

- Map 和 Set 都使用 `ShardBits` 指定的分片 `std::shared_mutex`；允许 `0..8`，默认 4（16 分片）。Map 的每片使用 Boost flat map，Set 仍使用 parallel-hashmap。同一 key 的更新会串行化；热点 key、差的哈希分布或长回调会限制吞吐。
- `try_emplace`、`insert`、`insert_or_assign` 返回 `bool` 表示是否新插入，**不返回迭代器**。`insert_or_assign` 返回 `false` 也可能已成功更新旧值。
- `try_emplace_or_visit(key, callback, args...)` 在底层一次受保护操作内完成“缺失则构造，否则更新”；回调仅在元素原先存在时调用。
- `visit(key, f)` 传入 `T&`，`cvisit(key, f)` 传入 `const T&`，返回是否找到 key。`find(key)` 返回 `std::optional<T>` 副本；`find/snapshot` 仅在元素可复制时可用。移动专属值可通过 visitor 访问。
- **回调执行期间不能重新访问同一容器，也不要持有一个容器的锁再访问另一个容器。** 跨容器反向加锁可能死锁；引用和指针不能从回调逃逸。回调保持短小，避免 I/O、等待或未知锁。
- `contains` 后紧接着 `find/erase` 是两个独立操作；需要复合原子操作时使用 visitor 或 `erase_if(key,predicate)`。不支持跨 key/跨容器事务。
- Map/Set 的 `size()`、`reserve()` 和 `clear()` 都逐分片加锁；观察元数据时持锁。
- `snapshot/size/reserve/clear` 都不提供全表一致性快照；`size()` 不能与其他操作组成事务。不同分片的观察可能来自不同时间点，`clear()` 返回后其他线程仍可写入。需要精确外部一致性时，先停止写入。
- `reserve(n)` 按平均分布为每片预留空间，不保证偏斜分布下完全不扩容；Map/Set 均只在受影响分片中扩容。
- Hash/Equal 必须对并发调用安全，键在容器中保持哈希/相等关系稳定。复杂用户类型、抛异常的哈希/比较/移动/构造及分配失败的完整异常恢复尚未验证；不承诺 STL 的强异常保证。可靠服务应使用稳定的无异常热路径类型并处理资源耗尽。
- 随包 `boost/` 固定为 1.87.0；同一工程需要其他 Boost 组件时，应保持版本一致。Map 的回调在持锁期间直接调用，支持有状态左值、移动专属回调与仅接受右值的回调。
- 本版没有自定义 allocator、带状态 Hash/Equal 的构造参数或异构查询接口；不能把这一层当作上游全部 API 的转发。

## 快速使用

把整个 `include/` 放入头文件搜索路径；发行时保留相应许可证。或者使用 CMake：

```cmake
add_subdirectory(path/to/cstl)
target_link_libraries(your_program PRIVATE cstl::cstl)
```

```cpp
#include <cstl/cstl.hpp>
#include <string>

cstl::concurrent_unordered_map<std::string, int> counts;

void record_request() {
    counts.try_emplace_or_visit("requests", [](int& n) { ++n; }, 1);
}

// 复制读取：返回值离开容器锁后仍有效。
auto total = counts.find("requests");
```

```cpp
cstl::concurrent_queue<int> messages;
{
    auto producer = messages.make_producer_token();
    if (!messages.push(producer, 42)) {
        // 业务选择重试、记录错误或中止；不能忽略失败。
    }
} // token 先于 messages 销毁

int message;
if (messages.try_pop(message)) {
    // 处理成功取出的消息。
}

cstl::spsc_queue<int> one_to_one(1024);
cstl::bounded_queue<int> fixed_mpmc(1024);
cstl::concurrent_priority_queue<int> priorities;
priorities.push(9);
```

## 构建、测试和安装

在已配置 C++ 工具链的终端中执行：

```sh
cmake -S . -B build -DCSTL_BUILD_TESTS=ON -DCSTL_BUILD_EXAMPLES=ON -DCSTL_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix /your/install/prefix
```

安装后可通过 `find_package(cstl CONFIG REQUIRED)` 和 `cstl::cstl` 使用。头文件库本身没有待编译的第三方二进制。

MSVC 建议使用 x64 Native Tools 终端或 Visual Studio CMake；GCC/Clang 直接编译示例可使用 `c++ -std=c++17 -O2 -pthread -Iinclude examples/basic.cpp`。本机实际验证平台为 Windows x64 / MSVC 19.44；0.3.0 同代码实现基线的跨平台 CI 记录见 `results/validation/ci.json`；1.0.0.0 发布提交的 CI 记录见 Release 附件 `validation-1.0.0.0.json`；0.2.0 历史 CI 已归档到 `results/v0.2.0/validation/`。没有单独安排 ARM 专项验证。

Linux CI 分别在 AddressSanitizer（ASan）和 ThreadSanitizer（TSan）下运行项目测试。支持相应 sanitizer 的 GCC/Clang 环境可通过 `-DCSTL_BUILD_TESTS=ON` 与 `-DCSTL_ENABLE_ASAN=ON` 或 `-DCSTL_ENABLE_TSAN=ON` 复现，两项使用独立的构建目录。TSan CI 加载 `.github/tsan.supp`，过滤 moodycamel 显式生产者入队路径上因 GCC TSan 无法识别原子栅栏而产生的报告。

`cstl_benchmark [threads] [operations_per_thread]` 仅测预填充整数哈希表的更新，对照 `std::unordered_map + mutex`，并校验最终总数。它不比较全部候选库，也不覆盖队列、读多写少、热点 key 或尾延迟，不能用于证明全场景最快。

```sh
python tools/verify_vendor.py
```

该离线工具验证提取文件及保留原件与清单中 SHA-256 的一致性；它不是安全审计或并发正确性证明。

## 来源与许可

精确 commit、原始文件位置、SHA-256 和移植改动见 [vendor_manifest.json](vendor_manifest.json)。未修改源码直接保留；移植版本在文件顶部标注变化，原件保存在 `provenance/`。默认构建不会编译原件。

新接口与测试采用 MIT；第三方源码分别保留 Apache-2.0、Boost-1.0、BSD 和 zlib 等原许可证，不能把整个库统一说成仅 MIT。详见 [THIRD_PARTY.md](THIRD_PARTY.md)、[NOTICE.md](NOTICE.md) 和 `licenses/`。

## Boost 替换与可复现对比

项目在 `baseline-handoff` Git 标签保留压缩包原始版本。原 Map 头文件另保存在 `provenance/cstl/`；benchmark 对照仅改变命名空间和 include 路径。

Boost 1.87.0 官方归档及 commit、SHA-256 记录于 `vendor_manifest.json`。`include/boost/` 包含完整 Unordered 实现和递归依赖。Map 改为完整 Boost flat map 加分片读写锁，以恢复原版的锁粒度和局部扩容。使用透明预哈希查询，未触发扩容的单键操作只调用一次用户 Hash（迁移旧键仍按表的需要重新哈希），不在每个元素中额外保存哈希。`detail/foa/core.hpp` 含少量可追溯定制：仅对本库内部 Hash 启用 14/15 的负载上限，并对平凡元素使用合法的平凡复制构造搬迁。未修改探测正确性、引用生命周期或内存序；未标记的普通 Boost Hash 保持官方负载上限和搬迁分派。原件、改动说明与 SHA 记录见 `provenance/boost/` 和 `vendor_manifest.json`。

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCSTL_BUILD_TESTS=ON -DCSTL_BUILD_EXAMPLES=ON -DCSTL_BUILD_BENCHMARKS=ON
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure
python tools/verify_vendor.py
python tools/benchmark.py --exe build-release/cstl_compare --output results
```

Windows 使用 `.exe`；多配置生成器使用 `build-release/Release/cstl_compare.exe`。MSVC 可指定 `-DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` 避免 PDB 服务依赖。

`-DCSTL_BUILD_UPSTREAM_TESTS=ON` 额外构建官方并发容器核心测试。Serialization 和跨进程测试依赖额外编译型 Boost 库，不在本头文件库的核心测试范围；具体执行范围见 `VALIDATION.md`。

性能矩阵同时比较压缩包的 parallel-hashmap 原版、直接 Boost 与 CSTL 封装；包括整数/字符串键、1/4/12 线程、65536 键及 64 热键、读取/未命中/更新/复合更新/混合/删除重插/预留插入/增长插入。每次检查正确性，随机交错运行，保留原始样本和置信区间。0.2.0 旧结果保存在 `results/v0.2.0/`。结果与限制见 `PERFORMANCE.md`，不能把有限硬件上的测量解释成所有负载性能完全相同。
