# P041 — Preferences

Status: DONE     Date: 2026-10-12     Commit: (this commit)

## Objective
Devices, paths, site and pointing, units, display and shortcuts edited in a dialog and written to the user's
`config.toml` only when the whole configuration validates; invalid values rejected with a clear message.

## Requirements covered
FR-PLT-03 (validated, human-readable configuration) from the user interface; NFR-USE-04 (messages name the key).

## Design notes
- `PreferencesDialog` collects a partial document (`[desktop]`, `camera.uvc`, `simulation.enabled`,
  `logging.level`) and hands it to `AppContext::update_user_config()`: the existing user file is read, the patch
  merged, the candidate written to `config.toml.candidate` and loaded **in place of the user layer** with every
  other layer through the normal loader, so the schema (`core/resources/config.schema.json`) and the typed reader
  decide; only then the user file is replaced atomically and the context reloads. Keys the dialog does not manage
  survive.
- Shortcut overrides are parsed first with the registry's action list, so a bad key names the action.
- Settings that need a restart (devices, renderer, log level) say so in their tooltips and in the notification
  after saving.

## Work log
1. `preferences_dialog.cpp`; `[desktop]` schema with ranges (latitude ±90, longitude ±180, altitude −500…9000 m,
   elevation ±90, roll ±180, azimuth 0…360, site id pattern).

## Verification
- `ui.valid preferences are written to the user file and reloaded`: site, theme and a shortcut override land in
  `config.toml` (`schema_version = 1`) and a fresh context reads them.
- `ui.an invalid value is rejected with a message that names the key, and nothing is written`: a site id with a
  space and an exclamation mark is refused with `desktop.site.id` in the message and no file is written; a
  shortcut `NotAKey!!` is refused naming `view.fit`.
- `ui.the user file keeps keys the dialog does not manage` (`simulation.seed` survives a save).

## Exit criteria
- [x] Invalid config rejected with clear message (tests above).

## Safety & failure-mode notes
- The candidate file is removed after validation; a crash between the two steps leaves at most a
  `config.toml.candidate` that the loader never reads.

## Deviations & next phase
- None. Next: P042 — Console & status.
