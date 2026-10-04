# Ph5: Toolchain Install — DONE

- MSVC Build Tools 2022 17.14 + VCTools workload (cl 19.44.35229 x64) ✅
- CMake 4.4.3 (`C:\Program Files\CMake\bin\cmake.exe`; needs new-shell PATH) ✅
- Ninja 1.13.2 (winget alias) ✅
- Notes: VS Installer pre-existed; BuildTools installed to
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`.
  `cl` stderr banner trips PS error stream — harmless, use `2>$null` when probing.
