# STRATIA inference benchmark (P070)

Model `stratia` 1.0.0-demo; ONNX Runtime 1.24.4; LEGION (Windows 11 Version 26H2); picture 4656 x 3496 (resized to the model's input by the host).

| Engine | Provider | Load (ms) | Preprocess (ms) | Mean (ms) | p50 (ms) | p95 (ms) | Min (ms) | Max (ms) | Runs | Note |
|---|---|---|---|---|---|---|---|---|---|---|
| `onnx:inference:directml` | DirectML (NVIDIA GeForce RTX 4050 Laptop GPU) | 1453 | 16.6 | 30.6 | 29.6 | 35.0 | 29.0 | 35.1 | 20 | |
| `onnx:inference:cpu` | CPU | 569 | 16.6 | 166.9 | 167.2 | 176.4 | 152.2 | 177.7 | 20 | |

Release build, 2026-10-22, development laptop (i7-14650HX, Intel UHD Graphics + NVIDIA GeForce RTX 4050 Laptop GPU, Windows 11). The DirectML engine runs on the adapter with the most dedicated video memory (the RTX 4050); with adapter 0, the integrated GPU, the same model took about 1,000 ms per frame, five times the CPU, which is why `InferenceService` times the chosen GPU against the CPU once at load and keeps the faster (P066). A 1,500-run DirectML measurement on a synthetic 1080p frame gave mean 29.4 ms, p50 29.2 ms, p95 30.8 ms; NVIDIA's own counters showed 95-97 % utilisation and 60-80 W during it.
