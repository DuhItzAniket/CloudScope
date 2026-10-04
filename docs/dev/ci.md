# Continuous integration

Workflow: [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml). It runs on every push to `master`, on pull requests and on demand. From P012 on, a phase is only complete when CI is green for its commit (`CONTRIBUTING.md`).

## Jobs

| Job | Machine | Toolchain | What it proves |
|---|---|---|---|
| Debian 13 x64 | GitHub x64 runner, `debian:trixie` container | GCC 14, Qt 6.8.2, Debian packages | The reference Linux build |
| Debian 13 arm64 | GitHub arm64 runner, `debian:trixie` container | Same, on real arm64 hardware | What a Raspberry Pi 5 with Raspberry Pi OS builds and runs |
| Ubuntu 26.04 x64 | GitHub x64 runner, `ubuntu:26.04` container | GCC 15, Qt 6.10 | Newest toolchain; finds problems before they reach Debian |
| Windows x64 | `windows-2022` | MSVC 2022, vcpkg, **Qt 6.8.3** installed with aqtinstall | The Windows build against the oldest supported Qt |
| Python tools | `ubuntu-24.04`, Python 3.11 | pytest | Sky logger tests and the tests of the CI scripts |

Each C++ job: configure with warnings as errors, build and test **Debug** and **Release**, run the licence check, upload the Release binaries with the licence report (`THIRD_PARTY_LICENCES.md`) as an artifact kept for 14 days.

The Linux jobs install dependencies with `tools/build/install-deps-debian.sh`, the same script a developer or a Raspberry Pi uses. The Windows job keeps compiled vcpkg packages in a cache between runs (first run about 25 minutes, later runs a few minutes) and caches the Qt installation.

## Reading the result without signing in

```
python tools/ci/status.py            # result for HEAD
python tools/ci/status.py --wait     # wait until all jobs have finished
```

Exit code 0 means every job passed. For a failed job the script prints the error lines and the end of the failing step's output.

How this works: every step runs through `tools/ci/run.py`, which passes the output through and, when the command fails, repeats the error lines and the last part of the output as GitHub *annotations*. Annotations are public for a public repository; full job logs need a signed-in user. The script uses the public API (60 requests per hour without a token; set `GITHUB_TOKEN` to raise the limit).

## Licence gate (ADR-010)

`tools/ci/licence_check.py` compares what a build links and ships with the reviewed table [`packaging/licences/third_party.toml`](../../packaging/licences/third_party.toml):

- **Windows:** every installed vcpkg port and every DLL next to the executables must belong to a listed component; a licence recorded by vcpkg must be covered by the table.
- **Linux:** every shared library the executables need directly must belong to a listed component.
- **Policy:** permissive licences are allowed with any linking; LGPL only for dynamically linked libraries; GPL-only components are not allowed in host binaries.

A new dependency therefore fails CI until someone adds an entry with its licence to the table. To run the check locally:

```
python tools/ci/licence_check.py windows --vcpkg-installed build/windows-msvc/vcpkg_installed --bin build/windows-msvc/bin/Release
python3 tools/ci/licence_check.py linux --bin build/linux-x64/bin/Release
```

On Linux the gate covers direct dependencies only: CloudScope's Debian packages will declare dependencies on distribution packages and not ship them. On Windows it covers everything that is shipped.

## Changing the workflow

- Actions are referenced by major version (`actions/checkout@v7`, `actions/cache@v6`, `actions/upload-artifact@v7`, `actions/setup-python@v7`); only GitHub's own actions are used.
- The workflow has read-only permissions and uses no secrets.
- Test a change to the Linux jobs locally first with the Docker image (`docs/dev/building.md`).
- To try a workflow change on GitHub without touching `master`, push it to a branch named `ci/<something>`: such branches run the workflow too. Read the result with `python tools/ci/status.py <commit>` and delete the branch afterwards.
