# Threads, events and the frame pipeline

How CloudScope code runs on several threads without tripping over itself. The thread model itself (which thread does what) is in `docs/arch/architecture.md` §4.1; this guide is about the building blocks (P016) and the rules for using them.

## Building blocks

| Need | Use | Header |
|---|---|---|
| Run work on another thread | `ThreadPool` (a pool of one thread is a serial executor) | `common/executor.hpp` |
| Get a result back from that work | `ThreadPool::submit()` → `std::future` | `common/executor.hpp` |
| Run work on the UI thread, or on any `QObject`'s thread | `QtExecutor` | `common/executor.hpp` |
| Tell listeners that something happened in *this object* | `Signal<Args...>` | `common/signal.hpp` |
| Tell *anyone who cares* that something happened | `EventBus` (the event's type is the topic) | `common/event_bus.hpp` |
| Hand camera frames to their consumers | `FramePool`, `FrameHub` | `capture/frame.hpp`, `capture/frame_hub.hpp` |
| Pass items from one thread to one other thread without locks | `SpscQueue<T>` | `common/spsc_queue.hpp` |
| Stop listening | `Connection::disconnect()`, or hold a `ScopedConnection` | `common/signal.hpp` |

## Rules

1. **Every class says which thread it lives on** in its header comment, and which of its functions may be called from other threads.
2. **Share nothing mutable.** Data is immutable once shared (frames), or owned by one thread and reached through an executor, or guarded by one named mutex. No bare `std::thread`: use a `ThreadPool` so that the thread has a name, survives a throwing task, and is joined.
3. **A listener chooses its thread, not the sender.** Subscribe with an executor (`signal.connect(executor, callback)`, `bus.subscribe<Event>(executor, handler)`) whenever the callback touches state that belongs to another thread. Without an executor the callback runs on the emitting thread and must be quick and thread-safe.
4. **Disconnect before you die.** `disconnect()` returns only when the callback is not running any more, so an object that holds its connections as `ScopedConnection` members (declared last, so they are destroyed first) can never be called after its destruction.
5. **The acquisition thread never waits.** It does not lock a contended mutex, allocate image memory, write files or log per frame. A full queue or an empty pool is a counted drop, not a pause (architecture §4.2).
6. **A slow consumer hurts only itself.** `Delivery::Queue` for consumers that need every frame (recorder): when they fall behind, *their* `dropped()` count rises. `Delivery::Latest` for consumers that need the current picture (preview, statistics, inference): older frames are replaced, `skipped()` counts them.
7. **Callbacks and tasks do not throw.** A `ThreadPool` logs an escaped exception and carries on; on an emitting thread the exception goes to the caller of `emit()`.
8. **No sleeping to wait for another thread** in product code or tests: wait on the thing itself (`FrameSubscription::wait`, a future, `ThreadPool::wait_idle`, `test::wait_until`).

## The frame path

```
camera driver thread                         consumer threads
--------------------                         ----------------
frame = pool.acquire()        (no buffer free -> count a drop, go on)
copy/decode into frame
frame->info = ...
hub.publish(frame)  ------->  recorder:  subscription->wait()   Queue: every frame, in order
                    ------->  preview:   subscription->wait()   Latest: only the newest
                    ------->  inference: subscription->wait()   Latest
```

A frame is written once, then shared read-only as `FramePtr`; consumers never copy pixels to receive it. When the last consumer lets go, the buffer returns to the pool. Frames produced by a simulator or generator carry `info.simulated = true`, and everything that stores or shows them must say so (NFR-DATA-03).

## Measuring it

```
cloudscope-bench                          4K BGR frames, as fast as possible
cloudscope-bench --fps 60                 paced like a camera
cloudscope-bench --width 4656 --height 3496       B0268 full resolution
cloudscope-bench --min-fps 60             exit code 1 unless reached without a dropped frame
```

Measured in P016 (Release builds, synthetic source, one producer, a queue consumer and a latest-only consumer):

| Machine | 3840 × 2160 BGR (24.9 MB) | 4656 × 3496 BGR (48.8 MB) | Dropped |
|---|---|---|---|
| Laptop, i7-14650HX, Windows 11, MSVC | 794 fps (19.8 GB/s) | 246 fps (12.0 GB/s) | 0 |
| Same laptop, Debian 13 in Docker, GCC 14 | 679 fps (16.9 GB/s) | 314 fps (15.4 GB/s) | 0 |

The requirement was more than 60 fps at 4K without drops. CI runs the 4K case on every platform with `--min-fps 60` (test `bench.4K frames ...`, Release only). These numbers are the cost of the hand-over itself (one copy per frame plus queueing); decoding, statistics and disk writes come on top in later phases and are measured there. A Raspberry Pi 5 has not been measured yet.

## Finding races

CI builds everything with the thread sanitizer and runs all tests (`Sanitizers (thread)`). Locally, in the Docker image:

```
docker run --rm --security-opt seccomp=unconfined -v "<repository>:/src" cloudscope-dev:trixie sh -c \
  "cmake --preset linux-x64 -B build/tsan -DCLOUDSCOPE_SANITIZE=thread && setarch -R cmake --build build/tsan --config Debug && setarch -R ctest --test-dir build/tsan -C Debug"
```

`setarch -R` switches address-space randomisation off for the command; without it the sanitizer stops with "unexpected memory mapping" on current kernels. The sanitizer only sees races in code that the tests actually run on several threads: a new shared structure needs a test that hammers it from several threads (see `tests/unit/test_signal.cpp`, `test_frame.cpp`, `test_spsc_queue.cpp`).
