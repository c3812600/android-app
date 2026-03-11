# GitHub Actions 自动构建 Android APK

这个项目演示了如何使用 GitHub Actions 免费自动构建 Capacitor Android APK。

## 📁 项目结构

```
github-actions-example/
├── .github/
│   └── workflows/
│       └── build-android.yml    # GitHub Actions 工作流配置
├── www/                          # 你的 Web 应用代码 (HTML/CSS/JS)
├── package.json                  # Node.js 项目配置
├── capacitor.config.json         # Capacitor 配置
└── README.md                     # 本文件
```

## 🚀 使用方法

### 1. 创建 GitHub 仓库

1. 在 GitHub 上创建一个新仓库
2. 将本目录内容推送到仓库

```bash
cd github-actions-example
git init
git add .
git commit -m "Initial commit"
git branch -M main
git remote add origin https://github.com/你的用户名/你的仓库名.git
git push -u origin main
```

### 2. 添加你的 Web 应用代码

将你的 Web 应用代码放入 `www/` 目录：

```
www/
├── index.html
├── css/
│   └── style.css
├── js/
│   └── app.js
└── ... 其他文件
```

### 3. 触发构建

**方式一：自动触发**
- 每次推送代码到 `main` 分支会自动触发构建

**方式二：手动触发**
1. 进入 GitHub 仓库页面
2. 点击 "Actions" 标签
3. 选择 "Build Android APK" 工作流
4. 点击 "Run workflow" 按钮

### 4. 下载 APK

构建完成后：

**方式一：Artifacts（构建产物）**
- 进入 Actions 页面 → 点击最新的工作流运行 → 下载 `android-apk` artifact

**方式二：GitHub Releases**
- 自动创建的 Release 页面会包含 APK 文件

## ⚙️ 配置说明

### 修改应用信息

编辑 `capacitor.config.json`：

```json
{
  "appId": "com.yourcompany.yourapp",  // 修改为你的应用 ID
  "appName": "Your App Name",           // 修改为你的应用名称
  "webDir": "www"
}
```

### 配置签名（Release 版本）

如需发布正式版，需要配置签名密钥：

1. 在 GitHub 仓库设置中添加 Secrets：
   - `KEYSTORE_BASE64`: 密钥库的 base64 编码
   - `KEYSTORE_PASSWORD`: 密钥库密码
   - `KEY_ALIAS`: 密钥别名
   - `KEY_PASSWORD`: 密钥密码

2. 修改 `.github/workflows/build-android.yml` 添加签名步骤

## 📧 自动通知（可选）

### 发送到钉钉

在工作流中添加：

```yaml
- name: Send to DingTalk
  uses: zcong1993/actions-ding@master
  with:
    dingToken: ${{ secrets.DINGTALK_TOKEN }}
    body: |
      {
        "msgtype": "markdown",
        "markdown": {
          "title": "APK 构建完成",
          "text": "### APK 构建完成\n- 版本: v${{ github.run_number }}\n- [点击下载](https://github.com/${{ github.repository }}/releases/tag/v${{ github.run_number }})"
        }
      }
```

### 发送到邮箱

```yaml
- name: Send Email
  uses: dawidd6/action-send-mail@v3
  with:
    server_address: smtp.gmail.com
    server_port: 587
    username: ${{ secrets.EMAIL_USERNAME }}
    password: ${{ secrets.EMAIL_PASSWORD }}
    subject: APK 构建完成
    to: your-email@example.com
    from: GitHub Actions
    body: APK 已构建完成，请前往 GitHub Releases 下载
    attachments: android/app/build/outputs/apk/release/*.apk
```

## 📝 注意事项

1. **免费额度**：GitHub Actions 免费账户每月有 2000 分钟构建时间，足够个人使用
2. **构建时间**：首次构建可能需要 5-10 分钟（需要下载依赖），后续会快很多
3. **缓存**：工作流已配置 Node.js 缓存，可加快构建速度

## 🔗 相关链接

- [Capacitor 文档](https://capacitorjs.com/docs)
- [GitHub Actions 文档](https://docs.github.com/cn/actions)
- [Android 开发者文档](https://developer.android.com/)
