# P016 — Threading & event bus

Status: DONE     Date: 2026-10-04     Commit: (this commit)

## Objective
The concurrency building blocks every later subsystem uses: a lock-free hand-over for camera frames, typed signals and an event bus, executors and worker pools; proven by tests under the thread sanitizer and by a benchmark.

## Requirements covered
NFR-PERF-01 (groundwork: the frame path costs far less than a frame period), NFR-PERF-03 (UI thread never blocked: listeners choose their thread), FR-AI-03 and FR-SEQ/FR-REC groundwork (latest-only versus lossless consumers), NFR-DATA-03 (`simulated` flag in frame metadata), architecture §4.1/§4.2.

## Design notes
New in `core/include/cloudscope/`:

| Header | What it provides |
|---|---|
| `common/spsc_queue.hpp` | `SpscQueue<T>`: bounded, lock-free, one producer and one consumer; a full queue rejects the push and leaves the item with the caller |
| `common/executor.hpp` | `IExecutor`; `InlineExecutor`; `QtExecutor` (runs on a `QObject`'s thread); `ThreadPool` (named workers, FIFO, `submit()` with futures, `wait_idle()`; a throwing task is logged and the worker lives on; the destructor finishes queued work) |
| `common/signal.hpp` | `Signal<Args...>`, `Connection`, `ScopedConnection`; a callback may be connected with an executor |
| `common/event_bus.hpp` | `EventBus`: publish/subscribe keyed by the event's type |
| `capture/frame.hpp` | `PixelFormat`, `FrameInfo` (sequence, timestamps, geometry, `simulated`), `Frame`, `FramePtr`, `FramePool` (buffers allocated once, 64-byte aligned, returned automatically) |
| `capture/frame_hub.hpp` | `FrameHub` and `FrameSubscription`: one producer, any number of consumers, each with `Queue` (every frame, counted drops) or `Latest` (newest only, counted skips) delivery |

Decisions:
- **The architecture's "ring buffer" is a pool plus per-consumer queues**, not one shared ring. A single ring with several readers forces either the slowest reader to block the producer or readers to be overwritten mid-read. With a pool of reference-counted buffers and one lock-free queue per consumer, a slow consumer loses only its own frames, and nobody copies pixels to share a frame.
- **Lock-free where it matters.** The producer's path for a `Queue` consumer is the lock-free queue plus a semaphore release. `Latest` delivery and the pool's free list use a mutex held for a pointer swap; that costs tens of nanoseconds against a 25 MB frame copy and keeps the code obviously correct.
- **`disconnect()` waits** until the callback is no longer running on any other thread, so an object that disconnects in its destructor cannot be called afterwards; a callback may still disconnect itself.
- **Signals are plain C++**, not Qt signals: they work for templates and for code without `QObject`, and `QtExecutor` is the bridge into Qt threads.
- **Benchmark as an executable** (`cloudscope-bench`) and as a pass/fail test in optimised builds (`bench.4K frames ...`, at least 60 fps and no dropped frame).
- Rules for using all this: `docs/dev/threading.md`.

## Work log
1. Wrote the six modules and 46 tests (6 queue, 11 executor, 17 signal and event bus, 12 frame, pool and hub).
2. Wrote `cloudscope-bench` and the benchmark test.
3. Added the thread-sanitizer CI job; made the sanitizer start (`setarch -R`) and dealt with false reports from an uninstrumented library (below).
4. Ran clang-tidy over the new files: 11 findings; fixed 4 in code, switched 2 analyser checks off with reasons.
5. Wrote the threading guide; updated the testing, quality-gate and CI guides.

## Verification
| Check | Result |
|---|---|
| Windows, MSVC 19.44, warnings as errors | 142/142 Debug · 143/143 Release (with the benchmark test) |
| Debian 13, GCC 14.2, warnings as errors | 141/141 Debug · 142/142 Release |
| AddressSanitizer + UndefinedBehaviorSanitizer, all tests | 141/141, no reports |
| ThreadSanitizer, all tests | 141/141, no reports in CloudScope code (see notes) |
| clang-format, clang-tidy on the new files | clean |

Benchmark (Release, one producer copying each frame into a pooled buffer, a `Queue` consumer and a `Latest` consumer):

| Machine | 3840 × 2160 BGR, as fast as possible | Paced at 60 fps | 4656 × 3496 BGR (B0268 size) |
|---|---|---|---|
| Laptop (i7-14650HX), Windows 11 | 794 fps, 19.8 GB/s, 0 dropped | 60.1 fps, 0 dropped, worst hand-over delay 0.20 ms | 246 fps, 0 dropped |
| Same laptop, Debian 13 in Docker | 679 fps, 16.9 GB/s, 0 dropped | not run | 314 fps, 0 dropped |

What the tests establish:
- **Queue:** order, full and empty, wrap-around, a refused push does not move from its argument, leftover items destroyed, one million items between two threads in order.
- **Executors:** FIFO order on a single worker; real parallelism on four; futures carry results and exceptions; a throwing task is logged and the worker survives; the destructor drains the queue; `wait_idle`; Qt executor runs on the object's thread through the event loop and drops tasks after the object died; thread names.
- **Signals and event bus:** call order and arguments; disconnect; self-disconnect; connect during emit; exception propagation without leaving the slot "running"; `disconnect()` blocks until a callback on another thread has finished; executor delivery with copied arguments; no queued call after disconnect; a queued call survives its signal; connect/emit/disconnect from six threads at once; events reach only subscribers of their type; publishing from a worker to a handler on the Qt thread.
- **Frames:** format sizes; aligned buffers; pool exhaustion returns nothing instead of waiting; buffers reused, metadata reset; frames outlive their pool; queue delivery in order with counted drops (oldest frames kept); latest delivery with counted skips; consumers independent and sharing the same frame object; subscription ends with its handle; `wait` with and without timeout; 20,000 frames through two consumer threads with every frame accounted for and none corrupted.

## Exit criteria
- [x] ≥ 4K frames copied at > 60 fps without drops (bench): 794 fps on Windows and 679 fps on Linux on the laptop, no drops; enforced in CI on every platform by the `bench.` test. CI result for this commit checked after the push.

## Safety & failure-mode notes
- **The thread sanitizer could not start** in the Debian container: "unexpected memory mapping", a clash between the sanitizer in GCC 14 and the address-space randomisation of current kernels. Commands that run sanitized programs are started with `setarch -R` (randomisation off), which needs the container option `seccomp=unconfined` in CI.
- **Nine false reports from Intel TBB.** All synthetic-sky tests failed under the thread sanitizer with a race in `operator delete[]`; both stacks of every report were inside `libtbb.so`, the uninstrumented thread pool OpenCV uses. Calls made from inside libtbb are now ignored through a suppression compiled into sanitizer builds (`cmake/tsan_suppressions.cpp`, with the reasoning). CloudScope's own code produced no report.
- **What the benchmark does not show:** consumers here only touch one byte per page. Decoding, statistics, encoding and disk writes will dominate later; each gets its own measurement in its phase. A Raspberry Pi 5 has not been measured.
- **`Latest` consumers can starve a pool** if they hold a frame for long: every held frame is a pool buffer. Pool size must cover the queue capacities plus one frame in hand per consumer; the camera pipeline (P022) sizes it and reports pool misses as drops.
- **A callback connected without an executor runs on the emitting thread**: a slow one stalls the emitter. The guide makes "subscribe with an executor" the rule for anything that is not trivial.
- A warning that had to be silenced on purpose: MSVC C4324 (padding from `alignas`) in the queue, where the padding is the point.

## Deviations & next phase
- The plan said "lock-free SPSC frame ring buffer"; delivered as a frame pool plus lock-free SPSC queues per consumer (reasons above). The exit criterion is met with this design.
- "Typed signals" are an own implementation rather than Qt signals (reasons above).
- clang-tidy: `clang-analyzer-cplusplus.NewDeleteLeaks` (false positive inside Qt's `invokeMethod`) and `clang-analyzer-optin.performance.Padding` are now off; 24 checks off in total, each with its reason.
- Next: **P017 — HAL interfaces**.
