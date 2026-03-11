#!/usr/bin/env python3
# -*- coding: utf8 -*-
"""
APK 构建助手
处理 Telegram 发来的 www 文件，自动推送到 GitHub 触发构建
"""

import os
import sys
import shutil
import subprocess
import tempfile
import json
from datetime import datetime

# 配置
GITHUB_REPO = "你的用户名/你的仓库名"  # 需要修改
EMAIL_TO = "你的邮箱@example.com"      # 需要修改
QQ_WEBHOOK = ""  # QQ机器人 webhook，可选


def trigger_github_build(www_path: str, commit_msg: str = "Update www"):
    """
    触发 GitHub Actions 构建
    
    Args:
        www_path: www 目录路径
        commit_msg: 提交信息
    """
    if not os.path.exists(www_path):
        raise FileNotFoundError(f"目录不存在: {www_path}")
    
    # 创建临时目录
    with tempfile.TemporaryDirectory() as temp_dir:
        repo_dir = os.path.join(temp_dir, "repo")
        
        # 克隆仓库
        print(f"⬇️  克隆仓库 {GITHUB_REPO}...")
        subprocess.run(
            ["git", "clone", f"https://github.com/{GITHUB_REPO}.git", repo_dir],
            check=True,
            capture_output=True
        )
        
        # 更新 www 目录
        www_dest = os.path.join(repo_dir, "www")
        if os.path.exists(www_dest):
            shutil.rmtree(www_dest)
        shutil.copytree(www_path, www_dest)
        
        # 提交并推送
        print("💾 提交更改...")
        subprocess.run(["git", "-C", repo_dir, "add", "."], check=True)
        
        # 尝试提交（如果没有更改会失败，忽略）
        try:
            subprocess.run(
                ["git", "-C", repo_dir, "commit", "-m", f"{commit_msg} - {datetime.now().isoformat()}"],
                check=True,
                capture_output=True
            )
        except subprocess.CalledProcessError:
            print("⚠️  无更改可提交，可能是重复构建")
        
        # 推送
        print("⬆️  推送到 GitHub...")
        subprocess.run(
            ["git", "-C", repo_dir, "push", "origin", "main"],
            check=True,
            capture_output=True
        )
    
    print("✅ 已触发 GitHub Actions 构建！")
    print(f"📊 查看进度: https://github.com/{GITHUB_REPO}/actions")
    return f"https://github.com/{GITHUB_REPO}/actions"


def main():
    """主函数"""
    if len(sys.argv) < 2:
        print("用法: python build_helper.py <www目录路径>")
        sys.exit(1)
    
    www_path = sys.argv[1]
    
    try:
        actions_url = trigger_github_build(www_path)
        print(f"\n🔗 Actions 页面: {actions_url}")
        print("⏳ 构建完成后会收到邮件通知")
    except Exception as e:
        print(f"❌ 错误: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
