# 选型与各项目优势的集成

目标：源码独立分发、C++17、通用并发读写、易于正确使用。选择比较的是数据结构及其具体语义，不能简单把不同算法的优点相加后宣称同时获得所有保证。

| 项目 | 重要优势 | 本版如何采用 | 有意保留的边界 |
|---|---|---|---|
| parallel-hashmap | flat 存储、分片并行、局部扩容、可配置锁 | 抽取 6 个必要头文件；Set 采用分片 `shared_mutex`；Map 原版保留作 benchmark 基线 | 锁竞争、热点 key、全表操作无单一快照 |
| moodycamel | 通用 MPMC 吞吐、生产者局部性、token、批量接口、阻塞变体 | 抽取 3 个必要头文件，提供通用与阻塞队列 | 不保证跨生产者全局 FIFO；初始容量不是硬上限 |
| Folly | SPSC 的简洁发布协议、游标缓存与缓存行隔离 | 移植 ProducerConsumerQueue，去除两项 Folly 支持头；标准分配器支持过对齐类型 | 仅 SPSC；64 位原子平台要求；不等于集成整个 Folly |
| oneTBB | 聚合多个并发操作后批处理，减少优先堆的直接竞争 | 移植优先队列和 aggregator；以标准原子等待、分配与异常替换运行时支持 | 有阻塞；没有带入 TBB 调度器；性能需要重新测量 |
| libcds | 有界原子环形队列、明确槽位生命周期、GC 可按结构选择 | 移植 VyukovMPMCCycleQueue 的保留/发布核心，以标准库替换 traits、buffer 等 | 此结构无需 HP/RCU；不把其他结构的内存回收代码混入队列 |
| Boost.Unordered | visitor 式并发 API，避免返回未受保护的引用 | Map 已切换为官方 1.87.0 完整 `concurrent_flat_map`，递归依赖随库分发，形成 `visit/cvisit/find(copy)` 和复合更新接口 | Boost 回调通常接收键值对；本版 Map visitor 接收 mapped value，不能直接混用示例 |

Folly ConcurrentHashMap 的 wait-free 读设计需要 hazard-pointer 回收及支持代码。libcds 的无锁链式 Map/Set、跳表也引入内存回收契约；oneTBB 的可增长 vector 则有独立的并发增长/元素访问语义。将这些全部移植进默认 Map 并不能同时保持原实现的所有优势。本版不把尚未移植的算法标为已支持。

本轮按用户要求将 Map 切换为 Boost 官方实现。Set 保持 parallel-hashmap。相同负载比较记录于 `PERFORMANCE.md`；结果存在负载依赖，不能声称所有情形都优于原版或已与 Folly 全面对比。

后续替换默认核心应先使用同一编译器/优化参数、键值类型、内存预算、读写比、线程数和数据分布进行比较，同时记录吞吐、P50/P99、内存和扩容/删除尖峰。先通过对应语义和并发测试，再决定替换；不能只凭峰值 benchmark 排名。
