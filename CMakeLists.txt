cmake_minimum_required(VERSION 3.16)

# Use vcpkg for package management
# Configure with: cmake -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake ..
# Install dependencies with: vcpkg install curl nlohmann-json

project(mcp-tutorial VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
add_subdirectory(examples)

# Optional: Enable testing
enable_testing()
add_subdirectory(tests)