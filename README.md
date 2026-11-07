# CSTL 并发容器

CSTL 是一个自包含的 C++17 头文件库。多个线程可以用它**共享按键查找的数据**，也可以用队列**把任务或消息从一个线程交给另一个线程**。库包含七种容器，所需的第三方头文件已随源码提供。

[怎么选](#怎么选容器) · [快速上手](#快速上手) · [实测性能](#实测性能) · [并发使用约定](#并发使用约定) · [构建与验证](#构建与验证)

## 怎么选容器

如果你要**按 ID 查数据**，先看 Map 和 Set。如果你要**把消息交给其他线程**，看下面的五种队列。

| 我想做什么 | 选哪个容器 | 典型用途 |
|---|---|---|
| 按键保存并更新一个值 | `concurrent_unordered_map` | 按用户 ID 累加请求次数 |
| 只记录某个键出现过没有 | `concurrent_unordered_set` | 按消息 ID 去重 |
| 一个线程发、一个线程收，要求先发先取 | `spsc_queue` | 把采集数据交给处理线程 |
| 多个线程收发，并限制最多积压多少条 | `bounded_queue` | 给线程池设置待办任务上限 |
| 多个线程收发，允许队列按需增长 | `concurrent_queue` | 多个解析线程把消息交给多个工作线程 |
| 队列为空时，让消费者睡眠等待 | `blocking_queue` | 工作线程平时休眠，有任务时再唤醒 |
| 优先处理更重要的任务 | `concurrent_priority_queue` | 优先处理严重级别较高的告警 |

### 队列会按什么顺序取？

| 队列 | 取出顺序 | 容量和等待方式 |
|---|---|---|
| `spsc_queue` | 单个生产者先放的元素先被取出 | 固定容量，满了或空了就返回失败 |
| `bounded_queue` | 按环形队列的槽位顺序取出。多个生产者同时入队时，以取得槽位的顺序为准 | 固定容量，满了或空了会返回失败。争用槽位时可能自旋 |
| `concurrent_queue` | 同一个生产者的消息保持顺序，不同生产者的消息可能交错 | 可增长，`try_pop` 立即尝试取一条 |
| `blocking_queue` | 与 `concurrent_queue` 相同 | 可增长，支持立即尝试、阻塞等待和限时等待 |
| `concurrent_priority_queue` | 默认先取最大值，同优先级的元素可能交错 | 可增长，空时 `try_pop` 返回失败 |

这里的顺序是**出队顺序**。两个消费者并行处理时，后取到的任务也可能先做完。

## 快速上手

### 把库加到项目

需要 C++17 和系统线程支持。可以把整个 `include/` 加到编译器的头文件搜索路径，或者在自己的 CMake 项目里使用下面的方式。分发库时请保留随附许可证。

| 使用方式 | 在 CMake 中添加 |
|---|---|
| 把 CSTL 源码放进自己的项目 | `add_subdirectory(path/to/cstl)` |
| 使用已安装的 CSTL | `find_package(cstl CONFIG REQUIRED)` |

两种方式都用 `target_link_libraries(my_app PRIVATE cstl::cstl)` 把库加到自己的目标，其中 `my_app` 是目标名。

### 示例一：两个线程更新同一个计数

`try_emplace_or_visit` 把「不存在就插入，已经存在就更新」做成一次受保护的操作。下面两个线程各记一次请求，最终输出 `2`。

```cpp
#include <cstl/concurrent_unordered_map.hpp>
#include <iostream>
#include <thread>

int main() {
    cstl::concurrent_unordered_map<int, int> requests;

    auto record_request = [&] {
        requests.try_emplace_or_visit(42, [](int& count) { ++count; }, 1);
    };

    std::thread first(record_request);
    std::thread second(record_request);
    first.join();
    second.join();

    std::cout << requests.find(42).value_or(0) << '\n';
}
```

### 示例二：给任务队列设定积压上限

队列最多放 1024 条。提交方检查 `try_push` 的结果，消费方只有在 `try_pop` 成功后才使用取出的值。

```cpp
#include <cstl/bounded_queue.hpp>
#include <iostream>

int main() {
    cstl::bounded_queue<int> tasks(1024);

    if (!tasks.try_push(42)) {
        std::cerr << "Queue is full\n";
        return 1;
    }

    int task{};
    if (tasks.try_pop(task)) {
        std::cout << "Task " << task << '\n';
    }
}
```

仓库里还有一个完整的多线程计数和阻塞队列示例。 [examples/basic.cpp](examples/basic.cpp)

## 实测性能

同一台 **Intel Core i7-9750H** 上，用 C++17 Release 跑了两套环境。每个场景预热后跑 7 次，取中位数。表中数字是所有线程的合计吞吐，线程由系统调度。

**测试数据全部使用平凡类型 `std::uint64_t`（8 字节无符号整数）**。Map 的键和值、Set 的键，以及五种队列的消息均使用这一类型。表中的吞吐适用于这个整数负载。

- **Windows**：原生 Windows x64，MSVC 19.44，`/O2`。
- **WSL**：Ubuntu 24.04 / WSL 2 x86-64，GCC 13.3，`-O3`。

### 哈希容器每秒能处理多少次

| 容器和操作 | 线程数 | Windows / MSVC（Mops/s） | WSL / GCC（Mops/s） |
|---|---:|---:|---:|
| Map 查找已有键 | 1 | 23.87 | 32.55 |
| Map 查找已有键 | 4 | 25.74 | 30.23 |
| Map 给已有键的值加一 | 4 | 18.81 | 15.04 |
| Set 查找已有键 | 1 | 24.92 | 28.83 |
| Set 查找已有键 | 4 | 20.87 | 22.81 |
| Set 删除一个键再插回去 | 4 | 5.83 | 2.06 |

**1 Mops/s = 10⁶ 次操作/秒。** 查找用的是 **65,536 个已存入的整数键**，每次都能找到。Set 的最后一行把一次删除加一次插入计为一次逻辑操作，各线程操作不同的键范围。

### 队列每秒能交接多少条消息

「一写一读」是 1 个生产者和 1 个消费者，「两写两读」是 2 个生产者和 2 个消费者。

| 队列 | Windows 一写一读（Mmsg/s） | WSL 一写一读（Mmsg/s） | Windows 两写两读（Mmsg/s） | WSL 两写两读（Mmsg/s） |
|---|---:|---:|---:|---:|
| `spsc_queue` | 68.24 | 51.53 | 仅支持一写一读 | 仅支持一写一读 |
| `bounded_queue` | 44.17 | 44.28 | 9.96 | 9.34 |
| `concurrent_queue` | 10.49 | 14.65 | 8.48 | 7.40 |
| `blocking_queue` | 6.53 | 6.48 | 6.00 | 5.32 |
| `concurrent_priority_queue` | 4.67 | 6.22 | 1.88 | 2.66 |

**1 Mmsg/s = 10⁶ 条消息/秒。** 一个 **8 字节整数**成功入队并被取出，算一条消息。每轮从空队列开始，交接 400,000 条。固定容量队列可放 1024 条，可增长队列的初始预留参数为 1024。阻塞队列使用 `wait_pop_for`。

两套环境的操作系统、编译器和优化选项都不同，表中的差异反映整套环境。比如 WSL 的 SPSC 单次结果约为 **20.24–118.04 Mmsg/s**，中位数是 **51.53 Mmsg/s**。这组测试测量的是持续交接吞吐，单条消息的等待时间和尾延迟需要另测。 [Windows 原始样本](results/container-benchmarks.csv) · [WSL 原始样本](results/container-benchmarks-gcc-wsl.csv) · [Map 与原版对照](PERFORMANCE.md)

<details>
<summary>在自己的机器上重跑</summary>

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCSTL_BUILD_BENCHMARKS=ON
cmake --build build --config Release --target cstl_container_bench
./build/cstl_container_bench
```

程序输出 CSV。计时排除容器构造、预填和最终校验。Windows 可执行文件带 `.exe`，Visual Studio 等多配置生成器在构建目录下另有 `Release/` 子目录。

用 GCC 复跑时，在新的构建目录配置并加上 `-DCMAKE_CXX_COMPILER=g++`。

`cstl_compare` 用于比较当前 Map、压缩包原版（`baseline-handoff`）和直接使用 Boost 的实现。用同一构建配置生成该目标后，可运行 `python tools/benchmark.py --exe build/cstl_compare --output results/local-map`。`cstl_benchmark` 是旧的整数更新对照程序，基线是 `std::unordered_map + mutex`。 [详细方法](PERFORMANCE.md)

</details>

## 并发使用约定

- **先构造容器，再交给线程。** 等所有访问它的线程退出后再销毁。对自己创建的线程，先完成 `join()`。
- **以操作的返回值为准。** `try_pop(out)` 返回成功才使用 `out`。对返回 `bool` 的入队接口，也要处理失败。大小估计适合监控，线程之间的协调需要独立的同步方式。
- **Map 和 Set 的访问回调要短。** 回调持有对应分片的锁。把需要的数据复制出来再做 I/O 或耗时处理，回调里避免重入同一容器或获取其他容器的锁。引用和指针的使用限于回调期间。
- **队列保护的是传进去的元素。** 如果元素是指针，多个线程之后同时访问指向的对象，仍要为该对象安排同步。

<details>
<summary>队列的容量、类型要求和停机细节</summary>

- **通用队列。** `push` 可以增加存储。`try_push` 不分配新的队列块，但首次隐式生产者注册仍可能分配。token 必须来自同一个队列，同一 token 同一时间只能由一个线程使用，并且要在队列销毁前释放。批量出队时，输出范围须至少容纳请求的最大条数。
- **阻塞队列。** `wait_pop` 可以一直等，停机由调用方协调。`wait_pop_for` 可用于定期检查停止条件，零或负时长按立即尝试处理，传入时长须能表示为有符号 64 位微秒。多生产者场景如果用终止消息，要先确认正常任务已经处理完，避免消费者提前退出。
- **SPSC 队列。** 只允许一个生产者和一个消费者。可用容量为 1 到 2³¹，实际存储槽数向上取整为二次幂。平台须支持始终无锁的 64 位原子操作。元素的移动赋值和析构须为 `noexcept`，入队构造可以抛异常。第三个观察线程可以读取 `size_approx()`，它只提供估计。64 位游标完整回绕后的行为不在契约内。
- **有界队列。** 容量须为至少 2 的二次幂。元素入队构造、出队移动赋值和析构须为 `noexcept`。队列存储预先分配，元素自身仍可能分配内存。槽位被占用但尚未发布时，其他操作可能自旋。接口没有硬实时等待上界。
- **优先队列。** 默认 `std::less<>` 先取最大值，改用 `std::greater<>` 可先取最小值。比较器须满足严格弱序且为 `noexcept`，元素的移动构造、移动赋值和析构也须为 `noexcept`。批次插入失败以 `std::bad_alloc` 报告。高优先级任务持续到来时，低优先级任务可能一直等待。`clear_unsafe()` 须在其他线程全部停止访问后调用。

</details>

<details>
<summary>哈希容器的返回值、锁和快照细节</summary>

- Map 的 `try_emplace`、`insert` 和 `insert_or_assign` 返回是否新增键。`insert_or_assign` 返回 `false` 时，原有值可能已经更新。Set 的 `insert` 返回是否新增键，可以直接用它做查重。
- Map 的 `visit` 和 `cvisit` 分别给回调传可写和只读引用，返回是否找到键。`find` 给出 `std::optional<T>` 副本。只可移动的值可在回调里访问。`contains` 再接 `find` 或 `erase` 是两次操作，需要一次完成时用相应的访问或条件删除接口。
- Map 和 Set 默认各有 16 个分片，`ShardBits` 可设为 0 到 8。同一分片的写入受分片锁保护。热点键、哈希分布偏斜、耗时回调都会影响并发度。
- `size`、`snapshot`、`reserve` 和 `clear` 按分片执行。观察可能来自不同时间点，需要全表一致的结果时先停止写入。`reserve(n)` 按平均分布预留容量，键偏向某个分片时该分片仍可能扩容。
- 自定义哈希和比较函数须能并发调用，键的哈希和相等关系在存储期间要保持稳定。公开接口不包含跨键事务、自定义分配器、带状态哈希或比较器构造参数，以及异构查询。容器不可复制或移动。

</details>

## 构建与验证

从源码构建示例和项目测试。MSVC 建议在 x64 Native Tools 终端或 Visual Studio CMake 中执行。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCSTL_BUILD_EXAMPLES=ON -DCSTL_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix /your/install/prefix
```

用 CLion 浏览源码时，可在 CMake Profile 加上 `-DCSTL_BUILD_EXAMPLES=ON` 并重新加载，让编辑器能从一个实际 C++ 目标获取头文件的编译上下文。已安装到自定义目录时，配置使用方项目可传入 `-DCMAKE_PREFIX_PATH=/your/install/prefix`。

GitHub Actions 在 **Windows x64、Linux x64、Linux ARM64 和 macOS** 上运行 Debug 和 Release 项目测试及 Boost 并发核心测试。独立检查包括 ASan/LSan、TSan、UBSan、MSan、Valgrind Memcheck、Clang 静态分析和 Map 锁检查，以及 Relacy 的队列槽位复用模型。TSan 的 moodycamel 显式入队报告过滤范围可查看规则。 [CI 配置](.github/workflows/ci.yml) · [TSan 规则](.github/tsan.supp) · [验证记录](VALIDATION.md)

## 实现来源和许可

| 容器 | 核心来源 |
|---|---|
| `concurrent_unordered_map` | Boost 1.87.0 `unordered_flat_map`，外加 CSTL 分片读写锁 |
| `concurrent_unordered_set` | parallel-hashmap 分片 flat hash set |
| `concurrent_queue` / `blocking_queue` | moodycamel ConcurrentQueue / BlockingConcurrentQueue |
| `spsc_queue` | Folly ProducerConsumerQueue 的独立移植 |
| `bounded_queue` | libcds VyukovMPMCCycleQueue 的核心循环移植 |
| `concurrent_priority_queue` | oneTBB 优先队列和 aggregator 的独立移植 |

同一工程若还使用其他 Boost 组件，请与随包的 Boost 1.87.0 保持一致。新接口和测试采用 MIT，第三方源码各自保留原许可证。来源、修改和校验值记录在清单中，可运行 `python tools/verify_vendor.py` 做离线完整性检查。 [来源清单](vendor_manifest.json) · [选型依据](SELECTION.md) · [第三方许可](THIRD_PARTY.md) · [发布说明](RELEASE_NOTES.md)
