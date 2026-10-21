# P071 — Model manager

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Install, update and roll back STRATIA models with checksum verification (FR-AI-08).

## Requirements covered
FR-AI-08.

## Design notes
- `core/ai/model_store`: a store folder (`<session_root>/models/`) with one sub-folder per installed package
  (`<name>-<version>/model_card.json`, the ONNX file, `install.json` with both files' SHA-256, the install time and
  the source) and `active.json` (`current`, `previous`). `install()` validates the card (P065), hashes the ONNX
  file and compares it with the checksum the package declares (STRATIA's `export_report.json` `onnx_sha256`, or a
  `<file>.sha256` next to the model); without a declared checksum the computed one is recorded and every later
  `verify()` checks the files against it. `activate()` verifies, then makes the model current and remembers the
  previous one; `rollback()` swaps them back (after verifying the previous files); `remove()` refuses the active
  model.
- Desktop `AiModelsDialog` (AI > Models…): the installed models with their checksums, Install… (folder chooser,
  verifies and activates), Activate, Roll back, Remove, and the Benchmark of P070. Activation tells the main
  window which folder to load; the window loads it and writes `ai.model_dir` into the user configuration, so the
  model is back at the next start. AI > Load model… stays as a one-off load without installing.

## Work log
1. Store, dialog, main-window wiring (`models_dialog()`, `show_models_dialog()`), the `ai.models` action.
2. Tests `tests/unit/test_ai_benchmark_store.cpp` (install with a computed and with a declared checksum, a
   tampered package refused with "checksum" in the reason, activate and roll back twice, corruption after
   installation caught by `verify()` and blocking activation and rollback, the active model cannot be removed,
   missing ids) and `tests/ui/test_ai_models.cpp` (the dialog installs, activates and rolls back and names the
   folder to load; activating in the window loads the model and remembers it in the user configuration).

## Verification
- `ctest -C Debug -R "unit\.the model store installs|ui\.(the models dialog installs|activating a model)"`: 3
  cases pass.

## Exit criteria
- [x] Rollback tested (store test and dialog test).

## Safety & failure-mode notes
A package is copied, then the copy is hashed again before it counts as installed; a mismatch removes the copy. A
model whose files changed after installation is refused at activation and rollback with the reason, so a corrupt
file cannot become the active model silently. The store never deletes the active model.

## Deviations & next phase
Model packages are installed from folders (STRATIA's `models/<version>/`); download and signature checks are
P097's release work. Next: P072 — Gate F.
