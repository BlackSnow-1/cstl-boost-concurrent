# Changes

## 0.2.0 (2026-11-04)

- Replace the Map core with unmodified Boost 1.87.0 concurrent_flat_map and bundle its header dependencies.
- Preserve the CSTL public mapped-value visitor API; ShardBits is a compatibility parameter only for Map.
- Keep original handoff under baseline-handoff and preserve a benchmark baseline.
- Add differential, exception/lifetime, atomic-update and custom-hash tests; official upstream core tests; cross-platform CI; reproducible comparison and raw results.
- Preserve all six other containers and upstream licenses.
