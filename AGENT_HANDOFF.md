# CSTL Boost 项目交接（2026-11-04）

项目目录：D:/GitHubProjects/cstl-boost-concurrent。GitHub：BlackSnow-1/cstl-boost-concurrent，私有仓库。CSTL 0.2.0，C++17，namespace/target 仍为 cstl/cstl::cstl。

Map 已使用 Boost 1.87.0 官方 concurrent_flat_map 完整实现。include/boost 内源码未改动；vendor_manifest.json 记录官方归档 SHA、commit 和逐文件 SHA。MPL/Preprocessor 使用宏选择的依赖也完整收录。构建时无需网络、外部 Boost 或编译型 Boost 库。

只重写 CSTL 接口和 mapped-value visitor 适配层，没有重写 Boost 探测、锁、内存序或扩容算法。临时可复制 visitor 按值保存以减少指针间接访问，左值及移动专属 visitor 按引用保存；保留原 visitor 状态语义。ShardBits 对 Map 仅为源码兼容参数；Set 继续使用 parallel-hashmap。其他六种容器核心和公开接口保持原样。

原始压缩包在 baseline-handoff 标签完整保留；原 Map、旧交接文档和旧验证记录保留在 provenance/cstl。benchmarks/baseline_map.hpp 仅调整原 Map 的命名空间与 include 路径，用于同二进制基线比较。

测试与性能：详见 VALIDATION.md、PERFORMANCE.md 和 results。基础测试为原 11 套，加 5 套 Map 扩展测试；可选官方并发核心测试为 32 个程序。Boost Serialization 和跨进程测试需要额外编译型库，不包含在核心测试范围。MSVC 代码生成证明只覆盖指定编译器的 uint64 读取/更新/插入，不得扩展成所有类型和负载零差异。

GitHub Actions 包括 Windows/Linux/macOS 的 Debug/Release，以及 Linux 官方核心测试。每次变更应运行相应验证；性能测量要与构建和其他测试隔离。不要根据单次 benchmark 改写第三方算法，也不要用缩小测试范围、改正确性语义或隐藏失败取得表面性能一致。
