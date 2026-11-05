# 验证记录：CSTL 0.3.0（2026-11-05）

本轮目标是消除相对压缩包原版的逐场景退步。0.2.0 的历史结果完整保存在 `results/v0.2.0/`，没有混算为本次验证。

## 本机执行

Windows x64、12 逻辑处理器、MSVC 19.44.35213、C++17、Ninja；Release `/O2 /Ob2 /DNDEBUG`，Debug `/Od`，使用 Embedded 调试信息。

- Release 和 Debug 均通过全部 **17 套项目测试**（原 11 套 + 6 套扩展），注册为 2 个 CTest 程序。
- Release 还通过全部 **32 个 Boost 官方并发核心测试程序**，合计 **34/34 CTest 程序，0 失败**，40.17 秒。
- 扩展测试包括 20 万次随机差分操作、异常恢复与对象生命周期、12 线程 60 万次原子计数、定制 Hash/Equal、回调类型、一次哈希检查、移动专属键及 20 万元素单片增长校验。
- 安装后独立 consumer 构建运行成功；8 个公开头文件分别以 MSVC `/W4 /WX` 单独包含编译成功。
- 原 11 套的 Map/Set 并发增删/观察、MPMC 80000、有界 MPMC 60000、SPSC 150000 条消息、阻塞等待和优先队列检查全部保留。
- 2082 个来源记录及保留的原始文件 SHA-256 校验。修改过的 Boost core 使用独立的修改后 SHA，官方原件和原 SHA 均保留，没有改写原 SHA 掩盖变化。

本轮没有增加每元素哈希元数据；完整的吞吐、固定参数重放及进程内存采样见 `PERFORMANCE.md`。

## 复现

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCSTL_BUILD_TESTS=ON -DCSTL_BUILD_EXAMPLES=ON -DCSTL_BUILD_BENCHMARKS=ON -DCSTL_BUILD_UPSTREAM_TESTS=ON
cmake --build build-release --config Release --parallel 3
ctest --test-dir build-release -C Release --output-on-failure
python tools/verify_vendor.py
python tools/verify_vendor.py --git-ref HEAD
```

Debug 单独使用 `-DCMAKE_BUILD_TYPE=Debug`。MSVC 可加 `-DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`。测试日志在 `results/validation/`。

## 验证边界

官方 32 个程序是 Boost.Unordered 的核心 concurrent 测试；Serialization 和跨进程测试需要额外编译型库，本轮未运行。没有运行整个 Boost 全库测试、ThreadSanitizer、长期耐久、完整分配失败注入或独立 ARM 专项测试。性能结论限定于已列出的本机负载，未测逐操作 P50/P99。

0.2.0 的 MSVC ICF 证明只针对旧的单个 Boost concurrent_flat_map 适配层，不作为 0.3.0 证据；探针源码归档在 `provenance/experiments/0.2/`。

GitHub Actions 当前版本已通过全部 7/7 jobs：Windows/Linux/macOS 的 Debug/Release 六组，以及 Linux 的 32 个官方核心测试。代码提交 `6ce94afcf262d471a47368aeff32189982e23565` 已验证；后续提交仅补充文档和 CI 记录，不修改 C++ 源码。工作流： https://github.com/BlackSnow-1/cstl-boost-concurrent/actions/runs/41941465440 。结果记录于 `results/validation/ci.json`，包括 Windows/Linux/macOS Debug/Release 和 Linux 官方核心测试。
