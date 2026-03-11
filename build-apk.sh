#!/bin/bash
# 自动构建 APK 脚本
# 用法: ./build-apk.sh <github_repo> <source_dir>

set -e

GITHUB_REPO="${1:-}"
SOURCE_DIR="${2:-./www}"
BRANCH="main"

if [ -z "$GITHUB_REPO" ]; then
    echo "❌ 错误: 请提供 GitHub 仓库地址"
    echo "用法: ./build-apk.sh owner/repo"
    exit 1
fi

echo "🚀 开始构建 APK..."
echo "📦 仓库: $GITHUB_REPO"
echo "📁 源码: $SOURCE_DIR"

# 创建临时目录
TEMP_DIR=$(mktemp -d)
echo "📂 临时目录: $TEMP_DIR"

# 克隆仓库
echo "⬇️  克隆仓库..."
git clone "https://github.com/$GITHUB_REPO.git" "$TEMP_DIR/repo"
cd "$TEMP_DIR/repo"

# 更新 www 目录
echo "📝 更新 www 目录..."
rm -rf www
mkdir -p www

if [ -d "$SOURCE_DIR" ]; then
    cp -r "$SOURCE_DIR"/* www/
else
    echo "⚠️  源码目录不存在，使用空目录"
fi

# 提交更改
echo "💾 提交更改..."
git add .
git commit -m "Update www from build script - $(date '+%Y-%m-%d %H:%M:%S')" || echo "无更改可提交"

# 推送触发 Actions
echo "⬆️  推送到 GitHub (触发 Actions)..."
git push origin "$BRANCH"

# 清理
rm -rf "$TEMP_DIR"

echo "✅ 已触发 GitHub Actions 构建！"
echo "📊 查看进度: https://github.com/$GITHUB_REPO/actions"
echo "⏳ 构建完成后会收到邮件通知"
