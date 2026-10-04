@echo off
call "%~dp0env.bat"
cmake -S desktop/app -B build/app -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64 -DOpenCV_DIR=C:/tools/opencv/opencv/build -DORT_ROOT=C:/tools/onnxruntime/onnxruntime-win-x64-gpu-1.22.0
