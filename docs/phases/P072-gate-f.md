# P072 — Gate F — AI pipeline

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Review the STRATIA integration end to end, on recorded sequences and on the live camera, and decide whether the
pipeline is ready for the AI-day demo of 2026-10-30.

## Requirements covered
The review covers FR-AI-01 to FR-AI-09 and NFR-PERF-05 (table in `docs/reviews/gate_F.md`).

## Design notes
The review runs the shipped application headless with the real exported model, the nominal B0268 calibration and
the demo configuration, and reads the evidence back from the catalogue (`ai_results`), so that what is reviewed
is what the demo will run. Two findings changed code during the review: the DirectML adapter index (the driver
had passed 0, the integrated GPU) and the timed provider check that keeps a GPU only when it beats the CPU.

## Work log
1. Headless runs: 45 s replay of the B0268 frames of 19 Oct 2026 on the RTX 4050 (43 results, none failed, all with
   geometry), the same before the adapter fix (the check switched to the CPU, 44 results), and 45 s on the live
   B0268 (87 results on the CPU at night); plus the uncapped 13-minute replay (700 results).
2. Benchmark of the exported model per provider (`docs/reviews/ai_benchmark.md`, P070).
3. Review document `docs/reviews/gate_F.md` with the requirement table, the model's zero-shot behaviour on this
   camera (recorded in STRATIA's research log for P083), the open items and the decision.
4. Stage bookkeeping: phase documents P065–P072, manual (`docs/manual/desktop.md` STRATIA section,
   `configuration.md` `[ai]` rows), `docs/dev/building.md` dependency rows, `docs/dev/testing.md`, ADR-004 notes,
   CHANGELOG, PROJECT_STATE, phase index, README tool row.

## Verification
- Release build and the whole suite in Release on Windows (`build/p072_release.bat`, 2026-10-22): 360 of 360 tests
  pass (unit, cli, ui and bench tiers); the Debug suite was green throughout the stage. Four Stage C and D tests
  that count the registry's drivers and devices were updated for the inference devices (`[ai] enabled = false` in
  their configurations; the mixed listing accepts the `onnx` driver).
- `python tools/dev/format.py --check`: all formatted; `tools/ci/licence_check.py windows` on the Release
  binaries: PASS (ONNX Runtime and DirectML listed).
- CI after the push: the run of this push is recorded in PROJECT_STATE.md (Last CI result checked) once it has
  finished.

## Exit criteria
- [x] Signed: decision GO in `docs/reviews/gate_F.md` (owner sign-off at the demo).

## Safety & failure-mode notes
The demo depends on three things that are now explicit: the model package path (`[ai] model_dir`), the
calibration file for the ray map, and the camera being enumerated at start (intermittent on the laptop: an owner
item in the review). A missing model or calibration degrades to "AI: not available" or unknown geometry, never to
a crash; a slow GPU degrades to the CPU by measurement.

## Deviations & next phase
Stage F was built before Stage E (owner decision of 2026-10-16, for the AI-day demo). Next: the demo on
2026-10-30; then Stage E — P047 CSDP spec v1 when the owner returns to hardware, and STRATIA P083 with the P041
labels.
