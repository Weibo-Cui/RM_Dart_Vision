cmake_minimum_required(VERSION 3.16)

#set(OpenVINO_DIR "/opt/intel/openvino_2024.6.0/runtime/cmake")

find_package(OpenVINO REQUIRED COMPONENTS Runtime)

# RKNN NPU 支持，默认关闭
# 启用：cmake -DUSE_RKNN=ON -DRKNN_SDK_DIR=/path/to/rknn_api ...
option(USE_RKNN "Build with RKNN NPU support (RK3588)" OFF)
if(USE_RKNN)
    set(RKNN_SDK_DIR "" CACHE PATH "Path to rknn_api lib (contains include/ and lib/librknnrt.so)")
    if(NOT EXISTS "${RKNN_SDK_DIR}/lib/librknnrt.so")
        message(FATAL_ERROR "USE_RKNN=ON but librknnrt.so not found under RKNN_SDK_DIR/lib (RKNN_SDK_DIR=${RKNN_SDK_DIR})")
    endif()
    include_directories(${RKNN_SDK_DIR}/include)
endif()

set(LIGHT_DETECT_SRC
    Detect.cpp
    OpenvinoInfer.cpp
    Traditional.cpp
    RknnInfer.cpp
)

add_library(light_detect OBJECT ${LIGHT_DETECT_SRC})

target_link_libraries(light_detect openvino::runtime tools)

if(USE_RKNN)
    target_compile_definitions(light_detect PUBLIC USE_RKNN)
    target_link_libraries(light_detect ${RKNN_SDK_DIR}/lib/librknnrt.so)
endif()