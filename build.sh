#!/bin/bash

# ============================================================================
# MCP 教程项目构建脚本
# ============================================================================
#
# 功能说明：
#   此脚本用于自动化构建 MCP (Model Context Protocol) 教程项目
#
# 构建产物：
#   1. libmcp_lib.a (498KB)         - MCP 核心静态库（包含配置、日志、JSON-RPC功能）
#   2. mcp_server (737KB)           - MCP 服务器主程序（使用 stdio 传输）
#   3. config_demo (650KB)          - 配置系统演示程序
#   4. test_logger (981KB)          - 日志系统单元测试（7个测试用例）
#   5. test_config (617KB)          - 配置系统单元测试（9个测试用例）
#   6. test_json_rpc (1.1MB)        - JSON-RPC 单元测试（4个测试用例）
#
# 使用方法：
#   ./build.sh          - 普通构建模式（Release，生成优化后的可执行文件）
#   ./build.sh asan     - ASAN 内存检测模式（Debug + AddressSanitizer，用于内存泄漏检测）
#
# 依赖项（通过 vcpkg 自动安装）：
#   - nlohmann-json: JSON 解析库
#   - spdlog: 日志库
#   - gtest: 单元测试框架
#
# ============================================================================

# 清理旧的构建目录（确保干净的构建环境）
rm -rf build

set -e  # 遇到错误立即退出

# ============================================================================
# 步骤 1: 检测构建模式
# ============================================================================
BUILD_MODE="normal"
if [ "$1" = "asan" ]; then
    BUILD_MODE="asan"
    echo "🔍 正在使用 ASAN 模式构建项目..."
else
    echo "🚀 正在构建 MCP 教程项目..."
fi

# ============================================================================
# 步骤 2: 创建构建目录
# ============================================================================
if [ ! -d "build" ]; then
    mkdir build
    echo "✅ 已创建构建目录"
fi

cd build

# ============================================================================
# 步骤 3: 检测 vcpkg 工具链
# ============================================================================
# vcpkg 是 C++ 包管理器，用于自动下载和编译依赖库
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

# ============================================================================
# 步骤 4: 使用 CMake 配置项目
# ============================================================================
echo "⚙️  正在使用 CMake 配置项目..."

# 根据构建模式选择不同的 CMake 配置
if [ "$BUILD_MODE" = "asan" ]; then
    # ASAN 模式: 使用 Debug 构建类型，启用 AddressSanitizer
    # AddressSanitizer 可以检测内存泄漏、越界访问等问题
    if [ -n "$VCPKG_TOOLCHAIN" ]; then
        cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN"
    else
        cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
    fi
else
    # 普通模式: 使用 Release 构建类型（编译器优化）
    if [ -n "$VCPKG_TOOLCHAIN" ]; then
        cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN"
    else
        cmake .. -DCMAKE_BUILD_TYPE=Release
    fi
fi

# ============================================================================
# 步骤 5: 编译项目
# ============================================================================
echo "🔨 正在构建项目..."

# 检测系统 CPU 核心数，以便并行编译（加快构建速度）
if command -v nproc &> /dev/null; then
    JOBS=$(nproc)                    # Linux 系统使用 nproc
elif command -v sysctl &> /dev/null; then
    JOBS=$(sysctl -n hw.ncpu)        # macOS 系统使用 sysctl
else
    JOBS=4                           # 默认使用 4 个并行任务
fi

# 使用 make 编译，-j 参数指定并行任务数
make -j$JOBS

echo ""
echo "🎉 构建完成！"
echo ""
echo "生成的文件位置："
echo "  - MCP 服务器:      build/src/mcp_server"
echo "  - 配置演示:        build/examples/config_demo"
echo "  - 日志测试:        build/tests/test_logger"
echo "  - 配置测试:        build/tests/test_config"
echo "  - JSON-RPC 测试:   build/tests/test_json_rpc"
echo ""
echo "运行测试: cd build && ctest"
echo "运行服务器: cd build/src && ./mcp_server"