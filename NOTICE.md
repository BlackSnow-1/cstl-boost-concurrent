# Third-party notices

CSTL includes selected source files and clearly marked ports from:

- Boost.Unordered 1.87.0 and transitive Boost headers, original authors as
  credited in each file; Boost Software License 1.0. Sources and core tests
  are byte-for-byte copies of the official release; see vendor_manifest.json.
- Parallel Hashmap, Copyright (c) 2019, Gregory Popovitch. This includes work
  from Abseil, Copyright 2018 The Abseil Authors, with modifications made by
  upstream Parallel Hashmap. Apache License 2.0 applies, except separately
  annotated portions such as Daniel James's Boost-licensed hash-combining code.
- ConcurrentQueue, Copyright (c) 2013-2020, Cameron Desrochers. Distributed
  here under its Simplified BSD license; the original file also offers the
  Boost Software License 1.0 as an alternative.
- LightweightSemaphore, based on Jeff Preshing's implementation, Copyright
  (c) 2015 Jeff Preshing, adapted and extended by Cameron Desrochers. The zlib
  license is embedded in `include/cstl/detail/moodycamel/lightweightsemaphore.h`.
- Folly ProducerConsumerQueue, Copyright (c) Meta Platforms, Inc. and affiliates,
  Apache License 2.0. The port removes Folly support dependencies and uses an
  aligned standard allocator; its SPSC publication algorithm is retained.
- oneTBB concurrent_priority_queue and aggregator, Copyright (c) 2005-2022
  Intel Corporation, Apache License 2.0. The port removes profiling/runtime
  dependencies, supplies standard atomic backoff and uses a standard allocator.
- libcds VyukovMPMCCycleQueue, Copyright (c) 2006-2018 Maxim Khizhinsky,
  Boost Software License 1.0; underlying bounded MPMC algorithm by Dmitry
  Vyukov, Copyright (c) 2010-2011, Simplified BSD license. The core loops are
  adapted to standard C++ storage/atomics without libcds traits or runtime.

Original source headers and license notices have been retained. Parallel Hashmap
and ConcurrentQueue core files are byte-for-byte copies. Modified ports explicitly
identify their changes and retain original copies under provenance/. The manifest
records exact revisions, original paths, local paths and SHA-256 digests.

CSTL is an independent integration and is not endorsed by the upstream projects.
