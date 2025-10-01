cmake_minimum_required(VERSION 3.16)

# Use vcpkg for package management
# Configure with: cmake -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake ..
# Install dependencies with: vcpkg install curl nlohmann-json

project(mcp-tutorial VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===================================================================
# ASAN (AddressSanitizer) Support
# Usage: cmake -DENABLE_ASAN=ON ..
# ===================================================================
option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")

    # ASAN compile and link flags
    set(ASAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer -g")

    # Add flags to compiler and linker
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ASAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ASAN_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ASAN_FLAGS}")

    # Optional: Enable leak detection (enabled by default on Linux)
    # You can disable it by setting ASAN_OPTIONS=detect_leaks=0
    add_definitions(-DASAN_ENABLED)
endif()

# Platform detection
if(APPLE)
    set(PLATFORM_NAME "macos")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Check for libcurl (using vcpkg)
find_package(CURL REQUIRED)

# Check for nlohmann/json (using vcpkg)
find_package(nlohmann_json REQUIRED)

# Check for spdlog (using vcpkg)
find_package(spdlog REQUIRED)

# Check for Google Test (using vcpkg)
find_package(GTest REQUIRED)

# Include directories
include_directories(src/include)

# Add subdirectories
add_subdirectory(src)

# 只在 examples 目录包含 CMakeLists.txt 时才添加
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# Optional: Enable testing
enable_testing()
add_subdirectory(tests)