# Gate F — AI pipeline (P072)

Date: 2026-10-21 · Reviewer: the project (owner sign-off pending at the AI-day demo of 2026-10-30) · Decision: **GO**

Gate F asks whether the STRATIA model runs end to end in CloudScope, on recorded sequences and on the live
camera, with the results shown and logged honestly (PLAN Stage F, P072: "End-to-end on recorded sequences and live
camera").

## What was run

The desktop application headless (`QT_QPA_PLATFORM=offscreen`, `--no-gamepad --reset-layout --exit-after 45`) with
the exported model `stratia-1.0.0-demo` (STRATIA P096), the nominal B0268 calibration
(`tests/data/stratia/b0268_nominal.camera.json`), the Bengaluru site and a zenith pose, `[ai] provider = "auto"`,
`rate_hz = 2`, Debug build; results read back from `<session_root>/catalogue.sqlite`, table `ai_results`.

| Run | Source | Engine | Results in 45 s | Mean run | Uncertain | Geometry valid |
|---|---|---|---|---|---|---|
| Replay (recorded sequence) | `sim:camera:replay` over `DATA/B0268/BLR01/2026-10-19` (4656 × 3496 JPEGs at 1 fps) | DirectML (NVIDIA GeForce RTX 4050 Laptop GPU) | 43 | 101 ms (first runs included; steady 54 ms) | 0 | 43 / 43 |
| Replay, same frames, before the GPU fix | as above | CPU (the provider check rejected the Intel GPU: 1,139 ms vs 172 ms) | 44 | 153 ms | 0 | 44 / 44 |
| Live camera (Release build) | `uvc:0c45:636d:1` Arducam B0268, 1920 × 1080 at 27.8 fps, 18:34 UTC (night, indoor light) | DirectML (NVIDIA GeForce RTX 4050 Laptop GPU), kept after the check (34 ms vs CPU 147 ms) | 88 | 80 ms (first runs included) | 0 | 88 / 88 |
| Live camera, earlier | the same camera at 16:16 UTC | CPU (before the GPU fix) | 87 | 191 ms | 0 | 87 / 87 |

Earlier, uncapped runs of the same pipeline logged 700 replayed results in 13 minutes without a failure (the
`--exit-after` value had been given in milliseconds).

## What the results say

- The pipeline holds: frames reach the model at the configured rate, every result is decoded, shown and logged
  with its provider, run time, reliability score and geometry flag; no run failed; the preview kept its frame rate
  while the model ran (Release, live camera: `camera_fps` 27.8, `display_fps` 28.0, 1,247 frames shown, 0 skipped,
  0 lost; the Debug run showed 7.5 fps on the raster renderer, as in Stage D).
- Provider selection works and is reported: with the adapter fix the application lists the three DXGI adapters,
  loads the model on the RTX 4050 and keeps it after the timed check (36 ms against the CPU's 160 ms); before the
  fix, the check measured the integrated GPU at 1,139 ms and switched to the CPU on its own.
- The model on this camera (zero-shot, unlabelled): on the daytime replay the reliability score stayed at
  0.9–1.2 against the card's threshold 1.457 (no frame flagged), the top genus was Cs or Ns, oktas 6–8, cover
  about 0.66 and a cloud base around 4.9 km, on frames that show blue sky with cumulus and cirrus. The model over-
  estimates cover and leans to stratiform genera on the B0268; this is recorded in STRATIA's research log as the
  first item for P083 (zero-shot evaluation with the P041 labels). The night run flagged nothing because indoor
  light kept the mean luminance above the 8 % night rule.

## Requirements of the stage

| Requirement | Evidence | Status |
|---|---|---|
| FR-AI-01 model card validated, incompatible models refused | `unit.cards that break the schema or the contract are refused`, `unit.a model's declared tensors are checked` | met |
| FR-AI-02 automatic provider selection, reported | log lines above; status bar `AI: … on DirectML (NVIDIA …)`; `unit.the service runs the dummy model on the best engine` | met |
| FR-AI-03 asynchronous, configurable rate, frames dropped not queued | `unit.the service … never queues frames`, `unit.the rate limit spaces inferences out`; replay run at 2 Hz with the preview unaffected | met |
| FR-AI-04 ray map and Sun metadata, unknown when unavailable | `unit.the ray map matches STRATIA's reference vectors`, `unit.unknown geometry gives the all-zero inputs`; `geometry_valid` 43/43 above | met |
| FR-AI-05 masks, layers, genus with confidence, cover, cloud base with interval | STRATIA panel and overlay (`ui.the STRATIA panel …`, `ui.the overlay picture …`) | met |
| FR-AI-06 uncertain results marked, overlays only from valid outputs | badge and hidden masks for uncertain results (`ui.the overlay picture … hides masks of uncertain results`) | met |
| FR-AI-07 per-frame logging and timelines | `ai_results` rows above; `ui.the main window logs every result …`, `ui.the panel … replays a session` | met |
| FR-AI-08 install, update, roll back with checksums | `unit.the model store installs, activates, rolls back …`, `ui.the models dialog …` | met |
| FR-AI-09 benchmark per device and provider | `docs/reviews/ai_benchmark.md`, `cloudscope-aibench` | met |
| NFR-PERF-05 the UI never blocks on inference | inference on its own thread, results by event; the preview rate during the runs | met |

## Open items

- The live camera dropped off the USB bus during the review: the run of 16:16 UTC found the B0268 and logged 87
  results; two later headless starts reported "driver 'uvc' has no device", and at 18:30 UTC
  `cloudscope-info --devices` no longer listed it at all (only the integrated webcam). After the owner reconnected
  it, the 18:34 UTC run opened it at once and logged 88 results on the RTX 4050. Owner item for the demo: seat the
  USB cable firmly, start the application with the camera connected, and use Camera > Refresh if it is missing.
- The timed provider check runs once at load and measures whatever the machine is doing at that moment: with a
  Release build compiling in the background it measured the CPU at 5.9 s against the GPU's 45 ms and kept the
  GPU, which was right, but on a loaded machine the comparison can go either way; the status bar and the log
  always say which engine is in use.
- Latency targets (NFR-PERF-05) come from STRATIA P094; the laptop's figures are in `ai_benchmark.md`.
- The B0268 zero-shot behaviour above is a model question for STRATIA P083/P084, not a gate failure.

## Decision

GO: the AI pipeline runs end to end on recorded sequences and on the live camera, with honest states and logged
results; the demo configuration is the one used here (`[ai] model_dir`, calibration file, site), on the RTX 4050.
