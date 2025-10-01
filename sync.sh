#!/bin/bash

# ===================================================================
# 代码同步脚本
# 功能：将本地代码同步到远程服务器
# 以本地代码为准，覆盖远程服务器上的文件
# ===================================================================

# 远程服务器配置
REMOTE_USER="root"  # 修改为你的SSH用户名
REMOTE_HOST="124.221.19.77"
REMOTE_PATH="/usr/team_project/mcp-tutorial"

# 本地项目路径（当前目录）
LOCAL_PATH="$(cd "$(dirname "$0")" && pwd)"

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}开始同步代码到远程服务器${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "本地路径: ${YELLOW}${LOCAL_PATH}${NC}"
echo -e "远程服务器: ${YELLOW}${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}${NC}"
echo ""

# 检查 rsync 是否安装
if ! command -v rsync &> /dev/null; then
    echo -e "${RED}错误: rsync 未安装${NC}"
    echo "请先安装 rsync: brew install rsync (macOS) 或 apt-get install rsync (Linux)"
    exit 1
fi

# 测试SSH连接
echo -e "${YELLOW}测试SSH连接...${NC}"
if ! ssh -o BatchMode=yes -o ConnectTimeout=5 "${REMOTE_USER}@${REMOTE_HOST}" "exit" 2>/dev/null; then
    echo -e "${RED}错误: 无法连接到远程服务器${NC}"
    echo "请确保："
    echo "1. SSH密钥已配置（或准备输入密码）"
    echo "2. 远程服务器地址和用户名正确"
    echo "3. 网络连接正常"
    exit 1
fi
echo -e "${GREEN}✓ SSH连接成功${NC}"
echo ""

# 确保远程目录存在
echo -e "${YELLOW}检查远程目录...${NC}"
ssh "${REMOTE_USER}@${REMOTE_HOST}" "mkdir -p ${REMOTE_PATH}"
echo -e "${GREEN}✓ 远程目录就绪${NC}"
echo ""

# 开始同步
echo -e "${YELLOW}开始同步文件...${NC}"
echo ""

# rsync 参数说明：
# -a: 归档模式，保留文件属性
# -v: 详细输出
# -z: 压缩传输
# --delete: 删除远程多余的文件（以本地为准）
# --progress: 显示传输进度
# --exclude: 排除不需要同步的文件/目录

rsync -avz --delete --progress \
    --exclude='.git/' \
    --exclude='.gitignore' \
    --exclude='build/' \
    --exclude='cmake-build-*/' \
    --exclude='.vscode/' \
    --exclude='.idea/' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.so' \
    --exclude='*.dylib' \
    --exclude='logs/' \
    --exclude='.DS_Store' \
    --exclude='*.swp' \
    --exclude='*~' \
    --exclude='vcpkg_installed/' \
    --exclude='node_modules/' \
    "${LOCAL_PATH}/" "${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}/"

# 检查同步结果
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}✓ 同步完成！${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "远程路径: ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}"
else
    echo ""
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}✗ 同步失败${NC}"
    echo -e "${RED}========================================${NC}"
    exit 1
fi
