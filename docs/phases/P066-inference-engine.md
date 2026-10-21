# P066 — Inference engine

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Run a STRATIA model through ONNX Runtime on the best available execution provider, asynchronously, dropping frames
rather than queuing them, and decode the contract's outputs into a result a host can show (FR-AI-02, FR-AI-03).

## Requirements covered
FR-AI-02, FR-AI-03, NFR-PERF-05.

## Design notes
- ADR-004. ONNX Runtime 1.24.4 is fetched at configure time: Windows takes the NuGet package
  `Microsoft.ML.OnnxRuntime.DirectML` (CPU and DirectML providers in one `onnxruntime.dll`) with its dependency
  `Microsoft.AI.DirectML` (`DirectML.dll`); Linux takes the release archive (CPU). CUDA is a runtime add-on: a
  runtime that reports it gets an `onnx:inference:cuda` device, no build change. `CLOUDSCOPE_ONNXRUNTIME=OFF`
  builds without the engine. Offline builds point `FETCHCONTENT_SOURCE_DIR_ONNXRUNTIME` / `_DIRECTML` at unpacked
  copies (`build/p065_check.bat`).
- `core/ai/onnx_engine`: `OnnxInference` is one `hal::IInference` per provider (`onnx:inference:cpu`,
  `onnx:inference:directml`, `onnx:inference:cuda`), `OnnxDriver` enumerates the providers the runtime reports,
  most preferred first; `load()` creates the session, `capabilities()` reports the declared tensors, `run()`
  checks names, shapes and sizes and returns the outputs in the declared order; `metadata()` reads the card STRATIA
  embeds in the ONNX file. `devices.cpp` adds the driver when `[ai] enabled` is true.
- `core/ai/inference_service`: `InferenceService::load()` tries the candidate engines in the order of
  `[ai] provider` (`auto` = CUDA, DirectML, CPU) and keeps the first that loads the model and declares the
  contract's tensors; `submit()` never blocks and never queues (a waiting frame is replaced and counted as
  dropped); the worker runs at most `rate_hz` times per second and calls back on its own thread. `make_inputs()`
  resizes the whole frame to the card's input (INTER_AREA, no crop, no mask zeroing, as the card says), converts
  BGR to RGB, scales to [0, 1] and normalises with the card's mean and std; `decode_outputs()` turns the seven
  tensors into a `Prediction`: genus and étage probabilities, oktas and the expected cover, the sky-parse and layer
  masks with class fractions, the cloud base's median and 80 % / 95 % intervals from the conformal radii of the
  card (CDF units, interpolated inside the bins), the reliability score and the `uncertain` flag (score above the
  threshold, or a night frame).
- Configuration `[ai]`: `enabled`, `model_dir`, `provider`, `rate_hz`, `threads` (`core/ai/settings`).
- Licences: ONNX Runtime (MIT) and DirectML (proprietary, redistributable: new policy list
  `allowed_redistributable`, text in `packaging/licences/DirectML-LICENSE.txt`). Thread sanitizer: calls from
  inside `libonnxruntime.so` are ignored like TBB's.

## Work log
1. CMake dependency block, runtime-file copy of `onnxruntime_providers_shared.dll` and `DirectML.dll`, licence
   table and policy, `[ai]` defaults, schema and manual rows, driver registration.
2. Engine, driver, service, settings; `benchmark` and `model_store` of P070/P071 build on them.
3. STRATIA side (P096 follow-up): `scripts/make_contract_reference.py` writes the dummy model's outputs for
   closed-form inputs; copied as `tests/data/stratia/reference.json`.
4. Tests `tests/unit/test_onnx_engine.cpp`: the engine obeys the HAL inference contract on the dummy model; the
   driver offers one device per provider with the CPU last; the dummy's declared tensors pass `check_model_io`,
   the embedded card reads back, and the outputs match STRATIA's reference to 1e-4 (all small heads in full, the
   dense heads by channel means and a 4 x 4 lattice of samples); inputs are built as the card says and hand-made
   outputs decode into the expected prediction (genus, étage, oktas, masks, layers, cloud base with ordered
   intervals, night and OOD flags, non-finite and missing outputs refused); the service loads the dummy on the
   best engine, runs a burst of 20 frames without queuing (run + dropped = 20, the newest frame runs last), the
   rate limit spaces runs; the `[ai]` section reads and the candidates follow the provider setting.

## Verification
- `ctest -C Debug -R "unit\.(the ONNX|the driver|the dummy|inputs are|the service|the rate|the \[ai\])"`: 7 cases
  pass on Windows. On this laptop the runtime reports DirectML and CPU; a DirectML session is created on the RTX
  4050 (benchmark of P070).
- Outputs equal the Python reference to ≤ 1e-4: `unit.the dummy model declares the contract's tensors and
  matches the Python reference`.
- Latency table per device and provider: `docs/reviews/ai_benchmark.md` (P070) for the real model on this
  laptop; the dummy runs in 2 ms (CPU) / 6 ms (DirectML).

## Exit criteria
- [x] Latency table per device/EP (`docs/reviews/ai_benchmark.md`).
- [x] Outputs equal Python reference ≤ 1e-4 (reference test above).

## Safety & failure-mode notes
A provider that cannot serve the machine (no DirectX 12 GPU, no CUDA libraries) fails at `load()` with
Unavailable and the service moves to the next one, so the model always ends on the CPU at worst; the provider in
use is reported (status bar, P068). A failed run is counted and logged, the previous result stays on screen. The
engine's buffers are the caller's vectors: inputs are copied into the engine's call, outputs out of it, so no
tensor outlives the call.

## Deviations & next phase
DirectML is used instead of CUDA on the development laptop: no CUDA toolkit is installed there and the DirectML
build needs nothing beyond DirectX 12. The CUDA path stays code-complete but untested. Next: P067 — Metadata
provider.
