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
    -ffast-math
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ANDROID_NDK}/sources/android/native_app_glue
)

# Arcade Engine Shared Library
# Include android_native_app_glue.c directly to ensure ANativeActivity_onCreate is exported
add_library(arcade_engine SHARED
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    src/game_profiles.cpp
    src/touch_input.cpp
    src/audio_engine.cpp
    src/renderer.cpp
    src/core_host.cpp
    src/main.cpp
)

# Ensure ANativeActivity_onCreate is not stripped and support 16 KB page alignment for Android 15+
target_link_options(arcade_engine PRIVATE
    "-u" "ANativeActivity_onCreate"
    "-Wl,-z,max-page-size=16384"
)

target_link_libraries(arcade_engine
    PRIVATE
    android
    log
    EGL
    GLESv3
    aaudio
    dl
)
