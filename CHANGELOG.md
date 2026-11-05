# Changes

## 1.0.0.0 (2026-11-05)

- First formal GitHub Release.
- Use the exact four-component version in CMake and release packaging.
- Mark header-only CMake package version metadata architecture independent.
- Preserve the verified 0.3.0 C++ container implementation without source changes.
- Ship full source, a relocatable CMake/header SDK and SHA-256 checksums.

## 0.3.0 (2026-11-05)

- Restore original shard-level shared/exclusive locking and local growth using complete Boost flat maps.
- Hash a key once per operation through transparent prehashed queries, without adding per-element hash metadata.
- Scope Boost core changes to the internal CSTL hasher: align growth limits and use trivial copy construction for eligible relocation; retain pristine provenance and verify both hashes.
- Add hashing-count, move-only-key lifecycle and 200000-element relocation regression checks.
- Retain 0.2.0 performance and validation records under results/v0.2.0.

## 0.2.0 (2026-11-04)

- Replace the Map core with unmodified Boost 1.87.0 concurrent_flat_map and bundle its header dependencies.
- Preserve the CSTL public mapped-value visitor API; ShardBits is a compatibility parameter only for Map.
- Keep original handoff under baseline-handoff and preserve a benchmark baseline.
- Add differential, exception/lifetime, atomic-update and custom-hash tests; official upstream core tests; cross-platform CI; reproducible comparison and raw results.
- Preserve all six other containers and upstream licenses.
