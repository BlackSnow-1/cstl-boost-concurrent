# 给接手 agent 的交接说明

## 用户目标与已确定偏好

- 从 GitHub 成熟 C++ 并发容器项目中抽取必要源码，形成一个 STL 风格的独立库。
- 使用时不安装、不链接原项目，也不在构建时下载依赖；允许 C++17 标准库和系统线程/信号量支持。
- 按数据结构分别选用合适的实现，集成不同项目的优势；默认负载是**通用并发读写，兼顾吞吐与易用性**。
- README 必须列出实际支持的容器，特别是各种队列，并明确使用边界。
- 用户希望交接整个项目给其他 agent。本压缩包解压后即是完整源码项目。

## 当前状态

本版为 CSTL 0.1.0，头文件库，命名空间 `cstl`，CMake target `cstl::cstl`。

实际支持 7 种容器：并发无序 Map/Set；通用 MPMC 队列；阻塞 MPMC 队列；SPSC 队列；有界 MPMC 队列；并发优先队列。没有实现完整 STL，也没有把全部上游项目搬进来。

已抽取/移植 parallel-hashmap、moodycamel、Folly、oneTBB 和 libcds 的所选结构。Boost.Unordered 仅为 visitor API 设计参考，未复制其实现。**尚无统一对比证据证明每种所选结构都是最快实现**，不要将选型结论改写成已经证明的性能排名。

## 建议阅读顺序

1. `README.md`：公开接口、全部队列的并发模型/顺序/容量/阻塞/关闭约束，以及 Map/Set 的访问边界。
2. `SELECTION.md`：逐项目采用的优点、没有采用的部分和选型证据的范围。
3. `VALIDATION.md` 与 `validation-results.txt`：已经执行和没有执行的验证。
4. `vendor_manifest.json`、`THIRD_PARTY.md`、`NOTICE.md`、`licenses/`：固定版本、原始来源和许可证。
5. `include/cstl/` 与 `tests/containers.cpp`：实际接口和行为。

## 目录导航

| 路径 | 内容 |
|---|---|
| `include/cstl/cstl.hpp` | 总入口 |
| `include/cstl/concurrent_unordered_map.hpp` | Map 接口；visitor、复制查找和复合更新 |
| `include/cstl/concurrent_unordered_set.hpp` | Set 接口 |
| `include/cstl/concurrent_queue.hpp` | moodycamel MPMC 队列、token 与批量接口 |
| `include/cstl/blocking_queue.hpp` | 限时/阻塞等待包装；没有 close |
| `include/cstl/spsc_queue.hpp` | Folly SPSC 移植的公开接口 |
| `include/cstl/bounded_queue.hpp` | libcds 核心循环的有界 MPMC 移植 |
| `include/cstl/concurrent_priority_queue.hpp` | oneTBB 优先队列移植的公开接口 |
| `include/cstl/detail/` | 源码依赖、移植实现和锁保护辅助函数 |
| `provenance/` | 修改过的上游头文件的原件，便于 diff 审查；不参与默认编译 |
| `tests/containers.cpp` | 11 组功能、并发和生命周期检查 |
| `examples/basic.cpp` | 多线程计数与阻塞队列示例 |
| `benchmarks/map.cpp` | 整数表更新的小型 benchmark，非完整跨库比较 |
| `tools/verify_vendor.py` | 离线检查提取/移植文件和保留原件的 SHA-256 |

## 复现验证

在项目根目录、已经配置编译器的终端中运行：

```sh
cmake -S . -B build -DCSTL_BUILD_TESTS=ON -DCSTL_BUILD_EXAMPLES=ON -DCSTL_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/verify_vendor.py
```

可再单独建立 Debug 构建目录。Windows 使用 Visual Studio x64 Native Tools 终端可减少工具链配置问题。此前本机的受限环境中，Visual Studio/MSBuild 的注册表读取和 `/Zi` 的 PDB 服务受到限制，改用已安装的 Ninja/MSVC 和 `/Z7` 后完成验证。这是当时执行环境的情况，不应直接写成库的通用安装要求。

此前本机中文编译器的 `/showIncludes` 输出编码导致 Ninja 依赖前缀识别不可靠，因此验证时做了干净重建。若修改头文件后出现意外的 “no work to do”，检查依赖跟踪，并用 `cmake --build build --clean-first` 确保实际重新编译。

安装验证：

```sh
cmake --install build --config Release --prefix install
```

在独立 consumer 项目中使用 `find_package(cstl CONFIG REQUIRED)`，配置时将安装前缀加入 `CMAKE_PREFIX_PATH`。现有版本已验证安装后构建运行，以及 8 个公开头文件分别单独包含并通过 MSVC `/W4 /WX` 编译。

## 必须保留的语义与移植事实

- Map/Set 采用分片 `shared_mutex`；不对外暴露裸迭代器或逃逸引用。`size/reserve` 通过 `detail/hash_observers.hpp` 逐分片加锁，避开底层平行表未加锁读取元数据的路径。
- Map 的 `visit/cvisit` 回调接收 mapped value，和 Boost 接收键值对的接口不同。全表大小、遍历、清理均不是一个原子快照。
- moodycamel 不保证跨生产者全局 FIFO。显式 token 需要正确的队列归属、线程使用和生命周期。
- `blocking_queue` 没有自动关闭；零/负等待时长被转换为立即尝试，避免底层将负值当作无限等待。
- Folly 移植移除了 CacheLocality/Bits 依赖，使用本地二次幂计算、64 字节缓存行隔离和标准 allocator，以支持过对齐对象。仅 SPSC；不承诺完整 64 位游标回绕后的行为。
- libcds 移植保留槽位保留/发布循环，改用标准原子和显式对象生命周期；没有引入 HP/RCU。`try_` 不代表固定等待上界。payload 的相关操作必须 `noexcept`，否则可能留下未发布槽位。
- oneTBB 移植保留 flat-combining heap/aggregator 核心，但替换了分配器、等待和异常支持，移除了 profiling/runtime 依赖。公开层限制抛异常的移动/比较，以免 combiner 退出后阻塞其他提交者。
- `detail` 不属于稳定 API；未修改的私有头仍有原项目命名空间/宏，与其他版本上游同处一个翻译单元可能冲突。

## 已验证和待验证

已验证：Windows x64、MSVC 19.38、C++17，Release 与 Debug 均通过 11 组检查；包含消息唯一性、顺序、并发增删/扩容观察、对象生命周期、等待以及安装后使用。详见随包验证记录。

尚未验证：Linux/macOS/ARM、ThreadSanitizer、长期耐久、完整异常/分配失败恢复、跨全部候选的公平吞吐/P99/内存比较，以及真实业务负载。本版没有生产长期运行证明。

下一步若用户要求继续开发，优先审查移植正确性与目标平台行为，再统一测试环境比较各候选；优化时保持各队列的不同语义。不能通过改变 FIFO/阻塞/容量语义来获得表面上更快的数字。

## 来源清单的维护

`vendor_manifest.json` 的每条记录含上游仓库、固定 commit、上游文件路径、本地路径与 SHA-256。移植文件还包含原件路径、原件 SHA-256 与改动描述。更新前可以按固定 commit 获取上游原文件，与 `provenance/` 比较；修改后同步更新对应改动记录和哈希，并重跑相关测试。不要只改哈希以掩盖无法解释的源码变化。

保留原项目版权和许可证。根目录 MIT 仅适用于新写接口、工具、测试等；不是整个库所有第三方代码的统一许可证。
