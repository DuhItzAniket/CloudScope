# Ph22: OpenCV 4.11.0 Prebuilt — DONE

- Downloaded `opencv-4.11.0-windows.exe` (185MB) → extracted to
  `C:/tools/opencv/opencv` (system-level, documented not committed).
- Verified: `OpenCVConfig.cmake` (vc16 x64) + `opencv_videoio_msmf4110_64.dll`
  (MSMF backend = USB/builtin camera support on Windows).
- CMake hint for app: `-DOpenCV_DIR=C:/tools/opencv/opencv/build`.
