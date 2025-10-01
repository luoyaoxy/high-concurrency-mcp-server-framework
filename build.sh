#!/bin/bash

# MCP 教程构建脚本
# 用于学习目的 - 仅限本地开发环境
#
# Usage:
#   ./build.sh          # Normal build
#   ./build.sh asan     # Build with AddressSanitizer (memory leak detection)

set -e

# Check if ASAN mode is requested
BUILD_MODE="normal"
if [ "$1" = "asan" ]; then
    BUILD_MODE="asan"
    echo "🔍 正在使用 ASAN 模式构建项目..."
else
    echo "🚀 正在构建 MCP 教程项目..."
fi

# 创建构建目录
if [ ! -d "build" ]; then
    mkdir build
    echo "✅ 已创建构建目录"
fi

cd build

# 检测 vcpkg 工具链文件位置
VCPKG_TOOLCHAIN=""
if [ -f "/root/.vcpkg/scripts/buildsystems/vcpkg.cmake" ]; then
    VCPKG_TOOLCHAIN="/root/.vcpkg/scripts/buildsystems/vcpkg.cmake"
elif [ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
else
    echo "⚠️  警告: 未找到 vcpkg 工具链文件"
    echo "   请确保已安装 vcpkg 并设置 VCPKG_ROOT 环境变量"
    echo "   或在命令中指定: cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
fi

# 使用 CMake 配置项目（使用 vcpkg 工具链）
echo "⚙️  正在使用 CMake 配置项目..."

# Build with or without ASAN
if [ "$BUILD_MODE" = "asan" ]; then
    # ASAN mode: use Debug build with ASAN enabled
    if [ -n "$VCPKG_TOOLCHAIN" ]; then
        cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN"
    else
        cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
    fi
else
    # Normal mode: Release build
    if [ -n "$VCPKG_TOOLCHAIN" ]; then
        cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN"
    else
        cmake .. -DCMAKE_BUILD_TYPE=Release
    fi
fi

# 构建项目
echo "🔨 正在构建项目..."
# 检测系统CPU核心数，以便并行构建
if command -v nproc &> /dev/null; then
    JOBS=$(nproc)                    # Linux 系统
elif command -v sysctl &> /dev/null; then
    JOBS=$(sysctl -n hw.ncpu)        # macOS 系统
else
    JOBS=4                           # 默认使用 4 个线程
fi

make -j$JOBS

echo ""
echo "🎉 构建完成！"