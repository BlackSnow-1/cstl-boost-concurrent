# CSTL 1.0.0.0

这是 cstl-boost-concurrent 的首个正式 GitHub Release。库使用 C++17，头文件自包含，提供 `cstl::` 命名空间和 `cstl::cstl` CMake target；构建时无需下载或链接原项目。

## 容器

- 并发无序 Map（完整 Boost flat map，分片读写锁和局部扩容）。
- 并发无序 Set。
- 通用 MPMC 队列、阻塞 MPMC 队列。
- SPSC 队列、有界 MPMC 队列、并发优先队列。

## 验证与性能

本版本的 C++ 容器源码与已验证的 0.3.0 逐字节相同；版本更新只涉及发布和 CMake 包元数据。功能验证包括 17 组项目测试及 32 个官方 Boost 并发核心测试程序，本机 Release 的 CTest 合计 34/34 通过。发布提交的 Windows/Linux/macOS Debug/Release 及 Linux 官方核心 CI 在正式发布前确认通过。

性能证据包括主矩阵 48 场景、旧退步场景原参数重放 14 组和增长边界 12 组，共 2790 个正式样本，未发现统计显著退步；逐场景 95% 区间下限不低于原版 0.95 倍的验收全部通过。主矩阵吞吐等权几何平均快约 12.7%，8 项内存采样的峰值 RSS 均低于原版。结论仅限列出的测量范围，原始数据及方法见 `PERFORMANCE.md` 和 `results/`。

## 发布文件

- `cstl-boost-concurrent-1.0.0.0-source.zip`：完整源码、测试、基准、原始数据、许可证、来源与修改记录。
- `cstl-boost-concurrent-1.0.0.0-sdk.zip`：可重定位的头文件与 CMake 安装包，包含所需 Boost 头文件及许可证。
- `validation-1.0.0.0.json`：发布提交、CI 结果和打包验证记录。
- `SHA256SUMS`：上述文件的 SHA-256 校验值。

SDK 解压后，将解压目录加入 `CMAKE_PREFIX_PATH`，通过 `find_package(cstl 1.0.0.0 EXACT CONFIG REQUIRED)` 和 `target_link_libraries(app PRIVATE cstl::cstl)` 使用。

各类队列的顺序、容量和等待语义见 README。第三方代码保留原许可证；根目录 MIT 不统一替代第三方许可证。
