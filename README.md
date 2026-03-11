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

### 沉浸式全屏适配

本仓库已内置沉浸式全屏方案，解决状态栏可见与底部白边问题：

1. 构建时由工作流写入 `MainActivity` 开启 Edge-to-Edge 并隐藏系统栏  
   - 文件由 CI 在构建阶段生成：`android/app/src/main/java/<appId>/MainActivity.java`  
   - 主要调用：
     - `WindowCompat.setDecorFitsSystemWindows(window, false)`
     - 使用 `WindowInsetsControllerCompat` 隐藏状态栏与导航栏，并允许手势短暂呼出
2. `capacitor.config.json` 中设置颜色，避免透明导致的白底：
   - `"statusBarColor": "#00000000"`（透明，内容延伸至状态栏）
   - `"navigationBarColor": "#000000"`（纯黑，避免底部白边）
3. Web 侧添加兜底背景色，避免透明时闪白：`body { background-color: #000 }`

如需保留系统栏显示但仍支持内容延伸至边缘，可将隐藏系统栏的代码去掉，仅保留 `setDecorFitsSystemWindows(false)`，或在应用内根据场景动态控制。

### 修改应用图标

- 自动生成（推荐）
  - 源文件位置（项目根目录）：`resources/icon.png` 或 `resources/icon.svg`（若存在 `resources/ico.svg` 会在 CI 中自动复制为 `icon.svg`）
  - 要求：正方形、≥1024×1024、无圆角、透明背景、主体居中
  - CI：工作流已自动执行生成步骤，无需手动命令
  - 本地调试可用命令：
    - `npm i -D @capacitor/assets`
    - `npx capacitor-assets generate --android`
  - 生成结果会写入 `android/app/src/main/res/mipmap-*` 与 `mipmap-anydpi-v26`（自适应图标）

- 手动替换
  - 文件路径（Android 项目内）：
    - `android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`
    - `android/app/src/main/res/mipmap-anydpi-v26/ic_launcher_round.xml`
    - `android/app/src/main/res/mipmap-*/ic_launcher.png`
    - `android/app/src/main/res/mipmap-*/ic_launcher_round.png`
  - 命名：保持 `ic_launcher` 与 `ic_launcher_round` 不变
  - 格式：PNG（建议带透明通道）；也可在 `ic_launcher.xml` 中引用前景/背景图层
  - 尺寸（传统位图密度）：
    - mdpi: 48×48
    - hdpi: 72×72
    - xhdpi: 96×96
    - xxhdpi: 144×144
    - xxxhdpi: 192×192
  - 自适应图标（Android 8.0+）建议：
    - 采用“前景 + 背景”两层，前景元素置于安全区内，背景可纯色或纹理
    - 建议从 1024×1024 源图导出前景/背景，避免超出裁剪蒙版

提示：若仓库未提交 `android/` 目录（由 CI 动态生成），可在工作流里先运行上面的 `@capacitor/assets` 命令或复制预置的 `res` 资源后再执行构建。

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
