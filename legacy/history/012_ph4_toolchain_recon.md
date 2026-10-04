# Ph4: Toolchain Recon — DONE

- Disk: **320GB free** on C: (plenty for full toolchain + Qt).
- Found via winget:
  - `Microsoft.VisualStudio.2022.BuildTools` 17.14.41
  - `Kitware.CMake` 4.4.3
  - `Ninja-build.Ninja` 1.13.2
- Missing (confirmed): cmake, cl, ninja, Qt, OpenCV, ONNX Runtime.
- Plan Ph5: install BuildTools (VCTools workload, quiet) + CMake + Ninja.
