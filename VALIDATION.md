# 验证记录（2026-11-04）

本记录来自本次运行；压缩包历史记录保存在 `provenance/cstl/`，不混算为本次验证。

## 本机已执行

平台为 Windows x64、Intel64 Family 6 Model 158 Stepping 10（12 逻辑处理器）、MSVC 19.44.35213、C++17、Ninja。Release 为 `/O2 /Ob2 /DNDEBUG`，Debug 为 `/Od`；采用 Embedded 调试信息避免本机 PDB 服务限制。

- 原始压缩包版本：Release 全部 11 套测试通过，示例通过。
- 最终版本：Release 和 Debug 各通过全部 16 套项目测试（原 11 套 + 新增 5 套）。CTest 将它们注册为两个测试程序。
- 最终 Release：额外通过 Boost 1.87 官方并发核心的全部 32 个测试程序。合计 **34/34 CTest 程序通过，0 失败**，总耗时 41.88 秒。
- 五套扩展检查：20 万次随机差分操作；异常恢复和移动对象生命周期；12 线程 60 万次原子计数及条件删除；自定义 Hash/Equal/ShardBits 兼容；有状态左值、移动专属及仅接受右值的回调。
- 原 11 套保留 Map/Set 增长/删除/并发观察、MPMC 80000 条消息、有界 MPMC 60000 条、SPSC 150000 条、阻塞等待、优先级和生命周期检查。
- 安装后独立工程通过 `find_package(cstl CONFIG REQUIRED)` 构建并运行；8 个公开头文件分别在 MSVC `/W4 /WX` 下单独包含编译通过。
- 官方归档 SHA-256 与发布方一致；`tools/verify_vendor.py` 校验 **2082** 个来源记录全部匹配。生产和测试依赖均保留原文件内容；MPL/Preprocessor 宏选择路径额外完整收录。
- `tools/codegen.cpp` 的 MSVC `/O2 /Gy /DNDEBUG`、链接器 `/OPT:REF /OPT:ICF` 实验：uint64 和 string 读取、更新、插入与直接 Boost 的函数地址分别相同，即链接器识别为相同机器码。结果见 `results/codegen.json` 和 `results/codegen-string.json`。

## 复现

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCSTL_BUILD_TESTS=ON -DCSTL_BUILD_EXAMPLES=ON -DCSTL_BUILD_BENCHMARKS=ON -DCSTL_BUILD_UPSTREAM_TESTS=ON
cmake --build build-release --config Release --parallel 3
ctest --test-dir build-release -C Release --output-on-failure
python tools/verify_vendor.py
```

Debug 单独配置 `-DCMAKE_BUILD_TYPE=Debug`。MSVC 如遇本机 PDB 问题可加 `-DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded`。

性能复现命令和结果见 `PERFORMANCE.md`。测试日志保存在 `results/validation/`。

## 验证边界

32 个官方程序对应 Boost.Unordered 的核心 concurrent 容器测试（包含 flat/node Map/Set、读写锁、异常恢复等）。官方 Serialization 和跨进程测试需要额外编译型 Boost 库，不在本项目头文件库的核心测试范围；没有把它们标为通过，也没有声称执行整个 Boost 全库测试。

本机没有运行 ThreadSanitizer、ARM、长期耐久、完整分配失败注入或实际业务负载。性能矩阵测试吞吐，未测逐操作 P50/P99 或独立的容器峰值内存；有限实验不能证明所有类型、平台及负载都性能等同。

GitHub Actions 提供 Windows/Linux/macOS Debug/Release 和 Linux 上游核心测试，远端结果以工作流记录为准。
