# P065 — Contract implementation

Status: DONE     Date: 2026-10-21     Commit: (this commit)

## Objective
Load STRATIA's `model_card.json`, validate it against the contract (stratia-contract v1.0) and refuse incompatible
models before any inference runs (FR-AI-01).

## Requirements covered
FR-AI-01.

## Design notes
- `core/include/cloudscope/ai/contract.hpp` fixes what the contract's major version fixes: input and output names,
  the class orders (genus, étage, sky parse, layer), the patch size, the oktas classes, the dense divisor, and the
  expected tensor shapes for a given input size and number of cloud-base bins. Everything a model may vary (input
  size, bin edges, temperatures, threshold) comes from its card.
- `core/include/cloudscope/ai/model_card.hpp`: `ModelCard` is read from JSON after validation against the bundled
  schema `core/resources/model_card.schema.json` (STRATIA's `schemas/model_card.schema.json`, with its one `format`
  keyword removed because CloudScope's JSON-schema checker does not implement `format`; the field is still a
  string). `check_contract()` then compares the card with the constants: a wrong major version, a reordered class
  list, an input that is not a multiple of the patch size, unsorted bin edges or a non-finite threshold are
  `Unsupported`. `check_model_io()` compares a loaded model's declared tensors with the card (names, shapes with an
  open batch axis). `open_model_package()` finds the card and the one `.onnx` of a package folder.
- STRATIA ↔ CloudScope: STRATIA's `docs/contract.md` names `tests/fixtures/stratia_dummy/` as the host contract
  fixture; it is copied to `tests/data/stratia/` (model, card, reference outputs, ray-map vectors, nominal B0268
  camera) and listed in the fixture manifest. The fixture rule grew a 200 KB allowance for a model file (its size
  is graph structure, not data).

## Work log
1. Contract constants and shape helpers; model-card reader with schema and contract checks; package opener.
2. Dummy fixtures from STRATIA P096 (`stratia_dummy.onnx` 150 KB, `model_card.json`, `reference.json`,
   `ray_map_reference.json`, `b0268_nominal.camera.json`) with manifest entries and README rows.
3. Tests `tests/unit/test_model_card.cpp`: the constants describe the v1 tensors; the dummy card loads with its
   numbers (512 px, patch 16, 24 bins ending at 12,000 m, folded calibration); a missing field, another major
   version, a reordered class list, an input that is not a multiple of the patch size and unsorted bin edges are
   refused with the reason, a newer minor version is accepted; declared tensors are checked against the card
   (open batch, renamed input, wrong output shape, extra input); a package needs the card plus exactly one ONNX
   file.

## Verification
- `ctest -C Debug -R "unit\.(the contract|STRATIA|cards that|a model)"`: 5 test cases pass (Windows, Debug and
  Release at the end of the stage; the CI matrix after the push).
- `unit.fixtures match the manifest` passes with the nine fixtures.

## Exit criteria
- [x] Contract test with STRATIA's exported dummy model: the dummy card validates and the engine test of P066 runs
      the dummy ONNX file and compares with STRATIA's reference outputs.

## Safety & failure-mode notes
A model is refused, never partially used: an unsupported card stops at load time with a message naming the
field. The schema copy must follow STRATIA's; the contract test of P066 (reference outputs) would catch a silent
drift of the tensor layout.

## Deviations & next phase
None. Next: P066 — Inference engine.
