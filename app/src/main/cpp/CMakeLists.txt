cmake_minimum_required(VERSION 3.22.1)

project("arcade_engine" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags for ultra-low latency execution
add_compile_options(
    -O3
    -flto
    -Wall
    -Wextra
    -fvisibility=hidden
    -ffast-math
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# Build standard Android native app glue
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
set_target_properties(native_app_glue PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Arcade Engine Shared Library
add_library(arcade_engine SHARED
    src/game_profiles.cpp
    src/touch_input.cpp
    src/audio_engine.cpp
    src/renderer.cpp
    src/core_host.cpp
    src/main.cpp
)

target_link_libraries(arcade_engine
    PRIVATE
    native_app_glue
    android
    log
    EGL
    GLESv3
    aaudio
    dl
)
