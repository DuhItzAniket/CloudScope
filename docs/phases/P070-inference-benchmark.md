# P070 — Inference benchmark tool

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Measure the model's latency per engine and execution provider from the command line and from the application,
and commit the laptop's report (FR-AI-09, NFR-PERF-05).

## Requirements covered
FR-AI-09, NFR-PERF-05, FR-AI-02 (the report names the adapter each provider ran on).

## Design notes
- `core/ai/benchmark`: `benchmark_model()` runs the package on every candidate engine (`all`, `auto` or one
  provider): session creation time, the host's preprocessing once, warm-up runs, then timed runs with mean, p50,
  p95, min and max of the engine's `run()` alone; engines that cannot load the model get a row with the reason.
  `benchmark_markdown()` and `benchmark_json()` format the report.
- `cloudscope-aibench` (`apps/aibench`): `--model <package> [--provider all|auto|cpu|directml|cuda] [--runs]
  [--warmup] [--threads] [--width --height | --picture file] [--json] [--report file.md]`; exit code 1 when no engine
  ran the model. The Models dialog's *Benchmark* (P071) runs the same function on a worker thread and shows the
  table, with *Save report…*.
- What the benchmark found on the development laptop (i7-14650HX, Intel UHD + RTX 4050 Laptop GPU, Windows 11,
  Release build, 20 runs; the model `stratia-1.0.0-demo` on a 4656 × 3496 B0268 frame):

  | Engine | Provider (adapter) | Load | Mean | p50 | p95 |
  |---|---|---|---|---|---|
  | `onnx:inference:directml` | DirectML (NVIDIA GeForce RTX 4050 Laptop GPU) | 1.5 s | 30.6 ms | 29.6 ms | 35.0 ms |
  | `onnx:inference:cpu` | CPU | 0.6 s | 167 ms | 167 ms | 176 ms |
  | `onnx:inference:directml` with adapter 0 (Debug) | DirectML (Intel UHD Graphics) | 1.1 s | ~1,000 ms | | |

  The first DirectML runs used adapter 0, the display adapter, which on a hybrid laptop is the integrated GPU: five
  times slower than the CPU. Two changes followed (P066 code): the engine picks the hardware adapter with the most
  dedicated video memory (DXGI enumeration; an explicit index is still honoured) and reports the adapter's name in
  the provider label; and `InferenceService` in `auto` mode times one run on the chosen GPU provider and one on
  the CPU after loading, and switches to the CPU when the GPU is slower (logged: "provider check: … slower than
  the CPU's …; using the CPU"). DirectML also receives a fixed batch dimension (`batch` = 1), which it needs to
  compile its kernels once.
- The committed report is `docs/reviews/ai_benchmark.md` (the Markdown of the run above, with a note on the
  adapter finding and a 1,500-run DirectML measurement: mean 29.4 ms, p95 30.8 ms). The host's preprocessing of a
  16-megapixel frame costs 17 ms in Release (61 ms in Debug).

## Work log
1. Benchmark function, Markdown and JSON reports, the `cloudscope-aibench` tool, the dialog's Benchmark button.
2. The adapter investigation above, with `dxgi_enum.py` (a standalone DXGI enumeration) to separate the process
   environment from the code: the enumeration was right, the driver had been passing adapter index 0.
3. Tests `tests/unit/test_ai_benchmark_store.cpp` (every engine reported with timings, Markdown and JSON, one
   provider only, an unknown provider refused) and `tests/ui/test_ai_models.cpp` (the dialog benchmarks on a
   worker thread and shows the table).

## Verification
- `cloudscope-aibench --model ../STRATIA/models/stratia-1.0.0-demo --runs 10 --warmup 2 --picture <B0268 frame>
  --report docs/reviews/ai_benchmark.md`: the table above; the log lists the three DXGI adapters and the one used.
- `ctest -C Debug -R "unit\.the benchmark reports|ui\.the models dialog benchmarks"`: pass.

## Exit criteria
- [x] Report committed (`docs/reviews/ai_benchmark.md`).

## Safety & failure-mode notes
A benchmark holds a second session of the model while it runs (the dialog's worker); on a machine short of GPU
memory a provider row fails with the reason rather than the application. The timed provider check at load costs
one extra CPU session (about a second) and two runs; it never runs in a configured non-`auto` mode.

## Deviations & next phase
Latency figures are from the Debug build on the laptop (NFR-PERF-05 targets come from STRATIA P094 later); the
Release figures are in the Gate F review. Next: P071 — Model manager.
