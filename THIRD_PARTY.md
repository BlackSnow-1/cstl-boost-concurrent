# Third-party source inventory

Exact revisions and SHA-256 digests are recorded in `vendor_manifest.json`.
The public library has no build-time downloads or upstream binary dependencies.

| Project | Extracted/ported files | License material |
|---|---|---|
| [parallel-hashmap](https://github.com/greg7mdp/parallel-hashmap) | phmap.h, phmap_fwd_decl.h, phmap_utils.h, phmap_bits.h, phmap_base.h, phmap_config.h | licenses/parallel-hashmap-APACHE-2.0.txt; separately marked Boost snippets covered by licenses/BOOST-1.0.txt |
| [ConcurrentQueue](https://github.com/cameron314/concurrentqueue) | concurrentqueue.h, blockingconcurrentqueue.h, lightweightsemaphore.h | licenses/concurrentqueue-LICENSE.md; semaphore's zlib text remains embedded in its header |
| [Folly](https://github.com/meta/folly) | port of folly/ProducerConsumerQueue.h | licenses/folly-APACHE-2.0.txt; original under provenance/folly/ |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) | ports of concurrent_priority_queue.h and detail/_aggregator.h | licenses/oneTBB-APACHE-2.0.txt; originals under provenance/oneTBB/ |
| [libcds](https://github.com/khizmax/libcds) | adapted core of cds/container/vyukov_mpmc_cycle_queue.h | licenses/libcds-BOOST-1.0.txt and licenses/VYUKOV-BSD.txt; original under provenance/libcds/ |

Parallel Hashmap and ConcurrentQueue header contents are unchanged. Folly,
oneTBB and libcds ports are explicitly modified derivatives. Their manifests
describe the removed dependencies and substitutions; the original copies allow
reviewing those changes. Upstream namespaces and macros in the unchanged private
headers remain present, so applications that also include different upstream
versions in the same translation unit need to check for conflicts. This initial
release does not promise coexistence with arbitrary versions of the same library.

Boost.Unordered's visitor API is a design reference; no Boost.Unordered source
is included. This project does not claim endorsement by any upstream author.

When redistributing source or binaries, preserve the corresponding notices and
license materials. The root MIT license applies only to the newly authored CSTL
components; it does not relicense bundled third-party implementations.
