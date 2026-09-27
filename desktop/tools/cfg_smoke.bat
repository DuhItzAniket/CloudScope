@echo off
call "%~dp0env.bat"
cmake -S desktop/smoke -B build/smoke -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64 -DOpenCV_DIR=C:/tools/opencv/opencv/build
