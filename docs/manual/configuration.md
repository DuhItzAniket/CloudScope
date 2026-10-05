# Configuration and logs

CloudScope reads its settings from TOML files. You only write the settings you want to change; everything else keeps its built-in default.

## Where the files are

| | Windows | Linux, Raspberry Pi OS |
|---|---|---|
| System file (all users) | `%PROGRAMDATA%\CloudScope\config.toml` | `/etc/cloudscope/config.toml` |
| User file | `%APPDATA%\CloudScope\config.toml` | `~/.config/cloudscope/config.toml` |
| Log folder | `%LOCALAPPDATA%\CloudScope\logs` | `~/.local/state/cloudscope/logs` |

Later files win over earlier ones: built-in defaults, then the system file, then the user file, then files given with `--config`, then overrides set by the application itself (for example from command-line options). Files that do not exist are skipped.

To see what is in effect and where it came from:

```
cloudscope-info --show-config
cloudscope-info --config my-test.toml      # also reads my-test.toml on top of the standard files
```

## Writing a configuration file

Every file starts with the format version, followed by the settings to change:

```toml
schema_version = 1

[logging]
level = "debug"
max_files = 10
```

The complete list of settings with their defaults is [`core/resources/config.defaults.toml`](../../core/resources/config.defaults.toml); the formal definition is [`config.schema.json`](../../core/resources/config.schema.json) (a JSON Schema that editors such as VS Code with a TOML extension can use for completion and checking).

### Settings in format version 1

| Key | Default | Meaning |
|---|---|---|
| `logging.level` | `"info"` | Messages below this level are dropped: `trace`, `debug`, `info`, `warn`, `error`, `critical` |
| `logging.console` | `true` | Write log lines to standard error |
| `logging.file` | `true` | Write log lines to `cloudscope.log` in the log folder |
| `logging.directory` | `""` | Log folder; empty means the standard folder in the table above |
| `logging.max_file_mb` | `10` | A log file is rotated when it would grow beyond this size (1 to 1024) |
| `logging.max_files` | `5` | Log files kept: `cloudscope.log`, `cloudscope.1.log`, … (1 to 100) |
| `simulation.enabled` | `true` | Offer simulated devices next to real ones (see below) |
| `simulation.seed` | `1` | The same seed gives the same simulated clouds and the same sensor noise (0 to 4294967295) |
| `simulation.replay_folder` | `""` | Folder of JPEG or PNG pictures that the replay camera shows in name order; empty means no replay camera |
| `simulation.replay_fps` | `2.0` | Pictures per second of the replay camera (0.01 to 120) |

More sections arrive with the features that need them.

## Simulated devices

CloudScope comes with simulated devices, so that it can be tried and tested without a camera or a mount: a sky camera with moving clouds, a pan-tilt mount, an orientation sensor, a GPS receiver and environment sensors. A second simulated camera replays your own pictures if you name a folder:

```toml
schema_version = 1

[simulation]
replay_folder = "D:/sky/2026-10-01"
replay_fps = 1
```

Use forward slashes in the folder name (or double every backslash): a single backslash has a special meaning in TOML. The pictures must lie directly in the folder; sub-folders are not searched.

Simulated devices are always marked as such: in the device list, in every picture they deliver, and in everything saved from them. They are never a measurement of the real sky. Set `enabled = false` to hide them.

To see which devices CloudScope can use with your configuration:

```
cloudscope-info --devices
```

```
Devices:
  sim:camera:sky          camera    Simulated sky camera  [simulated]
  sim:mount:pan-tilt      mount     Simulated pan-tilt mount  [simulated]
  sim:imu:head            imu       Simulated IMU on the camera head  [simulated]
  sim:sensor:gps          sensor    Simulated GPS receiver  [simulated]
  sim:sensor:environment  sensor    Simulated environment sensors  [simulated]
```

Drivers for real cameras and controllers are added in later versions; their devices will appear in the same list.

## Mistakes are reported, not ignored

A file with a syntax error, an unknown key or an invalid value is rejected as a whole, with a message that names the file and the key; CloudScope does not run on a half-read configuration:

```
Configuration error: C:\Users\asha\AppData\Roaming\CloudScope\config.toml:
  logging.level: must be one of "trace", "debug", "info", "warn", "error", "critical"; got "loud"
  logging.max_fiels: unknown key (did you mean 'max_files'?)
```

`cloudscope-info --config <file>` checks a file without starting anything else.

## When the format changes

A newer CloudScope may use a newer format version. It then converts your file automatically when it starts:

- the old file is kept next to the new one as `config.toml.v<old version>.bak` (an existing backup is never overwritten);
- the file is rewritten in the new format. **Comments are not carried over**; copy them from the backup if you need them;
- if the file cannot be rewritten (for example a system file you may not change), CloudScope converts it in memory at each start and says so.

A file written by a newer CloudScope than the one running is refused with a message; it is never guessed at.

## Logs

Each line has the UTC time, level, thread, component and message:

```
2026-10-04T12:34:56.789+00:00 info     14208 [camera] opened B0268 at 4656x3496
```

Passwords, tokens, API keys and `Authorization` values are replaced by `[redacted]` before a line is written anywhere.
