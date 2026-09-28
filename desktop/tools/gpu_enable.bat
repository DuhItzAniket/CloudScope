@echo off
rem Copies CUDA 12 + cuDNN redist DLLs next to the portable exe (dev machine).
set SRC=C:\tools\nvredist\dlls
set DST=%~dp0..\..\package\CloudScope
if not exist "%SRC%\nvidia" (
  echo nvredist not found. See .github/history/029_ph26b_gpu_enable.md for the pip recipe.
  exit /b 1
)
for /R "%SRC%" %%f in (*.dll) do copy /Y "%%f" "%DST%\" >NUL
echo GPU DLLs staged. Run cloudscope_app.exe and check backend=cuda.
