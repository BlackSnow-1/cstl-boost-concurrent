# CSTL Boost 0.3.0 交接（2026-11-05）

项目为 D:/GitHubProjects/cstl-boost-concurrent，私有 GitHub 仓库 BlackSnow-1/cstl-boost-concurrent。C++17，namespace/target 仍为 cstl/cstl::cstl。

用户明确要求消除与压缩包原版的性能差距。0.3.0 改为完整 Boost unordered_flat_map + 原版粒度的分片 shared_mutex 和分片增长；ShardBits 再次控制实际分片数。透明预哈希查询避免一次单键操作重复调用 Hash；没有增加每元素哈希字段。回调直接转发，保留左值状态、移动专属及仅接受右值的类型。

Boost detail/foa/core.hpp 有经过记录的少量定制：内部 Hash 的 14/15 负载上限对齐正常表的增长阈值；只对平凡复制/析构且使用标准分配器的元素启用平凡复制构造搬迁，并内联搬迁辅助函数。未标记 Hash 保持官方负载上限与原搬迁分派。官方原件在 provenance/boost/；所有修改均可由 vendor_manifest.json 的 source_sha256、original_copy、sha256 和 changes 追溯。

0.2.0 结果完整归档到 results/v0.2.0/，其源码可通过历史提交 9f504ce 查看。该版原生 Boost 热点 ICF 证明不代表 0.3.0。原始压缩包导入为 baseline-handoff（最初 Git 导入有换行规范化）；当前树已恢复第三方原字节，原清单 SHA 不变。原接口位于 provenance/cstl/，benchmark 的旧版只改命名空间/include 路径。

全量测试为原 11 套 + 新 6 套，另外保留 32 个官方核心测试程序。验证及测量范围、原始样本和结果见 VALIDATION.md、PERFORMANCE.md、results/。不要以平均收益代替逐场景对照，也不要把有限场景的无显著回退解释为任意硬件、键类型或业务负载都严格等速。

其他六个容器不变。构建无需网络、外部 Boost 或编译型 Boost 库；许可证和原始版权均保留。
