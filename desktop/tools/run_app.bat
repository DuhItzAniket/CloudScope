@echo off
rem CloudScope launcher. Works from desktop/tools/ (repo) or copied next to the exe.
if exist "%~dp0cloudscope_app.exe" (
  cd /d "%~dp0"
) else (
  cd /d "%~dp0..\..\package\CloudScope"
)
if not exist "cloudscope_app.exe" (
  echo cloudscope_app.exe not found. Build + package first; see desktop/package_README.md.
  pause
  exit /b 1
)
if not exist "models\cloudscope_b0268_expa3.onnx" (
  echo classifier ONNX missing in models/.
  pause
  exit /b 1
)
if not exist "models\cloudscope_seg_v2.onnx" (
  echo segmenter ONNX missing in models/.
  pause
  exit /b 1
)
echo Models OK. Backend check:
cloudscope_app.exe --infertest "..\..\B0268\b0268_005.jpg" 2>NUL | findstr /C:"backend=" /C:"class="
if errorlevel 1 (
  echo Self-test image not found; skipping backend check.
)
echo Starting CloudScope GUI...
start "" "cloudscope_app.exe"
