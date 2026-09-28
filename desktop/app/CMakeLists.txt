cmake_minimum_required(VERSION 3.21)
project(cloudscope_app LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2 Widgets)
find_package(OpenCV REQUIRED)
if(NOT DEFINED ORT_ROOT)
  if(DEFINED ENV{ORT_ROOT})
    set(ORT_ROOT $ENV{ORT_ROOT})
  else()
    set(ORT_ROOT "C:/tools/onnxruntime/onnxruntime-win-x64-gpu-1.22.0")
  endif()
endif()

set(SOURCES
  src/main.cpp
  src/CameraSource.cpp
  src/FrameProvider.cpp
  src/AppController.cpp
  src/OnnxInfer.cpp
  src/CloudVision.cpp
)
set(HEADERS
  src/CameraSource.h
  src/FrameProvider.h
  src/AppController.h
  src/OnnxInfer.h
  src/CloudVision.h
)

add_executable(cloudscope_app ${SOURCES} ${HEADERS} qml/qml.qrc)
target_include_directories(cloudscope_app PRIVATE src "${ORT_ROOT}/include")
target_link_directories(cloudscope_app PRIVATE "${ORT_ROOT}/lib")
target_link_libraries(cloudscope_app PRIVATE
  Qt6::Core Qt6::Quick Qt6::QuickControls2 Qt6::Widgets ${OpenCV_LIBS} onnxruntime)
target_compile_definitions(cloudscope_app PRIVATE
  $<$<CONFIG:Debug>:QT_QML_DEBUG>)
