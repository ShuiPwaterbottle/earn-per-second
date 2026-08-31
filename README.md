# EarnPerSecond · 每秒工资计算器

一个帮你 **亲眼看到时间就是金钱** 的小工具：设置你的工资，点一下「开始上班」，就能看到钱以每秒为单位不断进账 💰。

纯 Win32 C++ 编写，无任何第三方依赖，单个 `.exe` 即开即用。

## ✨ 功能

**两页式界面**：
1. **设置页** —— 填写工资信息：
   - 三种工资模式：月薪 / 日薪 / 时薪，配合每天工作小时数与每周工作天数，实时预览**每秒工资**与每小时等效值；
   - 显示累计收入，可一键重置。
2. **计时页** —— 点击「开始上班」后进入纯计时界面：
   - 大字显示每秒刷新的已赚金额（高精度计时，精确到分）；
   - 「暂停 / 继续」（暂停时间不计入）、「下班结算」弹出汇总（本次时长 + 收入 + 累计）；
   - 「返回设置」可随时回到设置页（工作中返回 = 自动结算，不丢数据）。

其他特性：
- **配置持久化**：工资设置与累计收入自动保存到 `salary.ini`（优先程序所在目录，其次 `%APPDATA%\EarnPerSecond`），下次启动自动恢复；
- **自动结算**：工作中直接关窗会自动结算并保存；
- **渲染健壮性**：父窗口带 `WS_CLIPCHILDREN` + 创建后全量重绘子控件，杜绝控件空白；
- **刻意不启用 DPI 感知**（无 manifest、不调用 `SetProcessDPIAware`），由系统统一缩放，保证任何显示缩放下都完整渲染（实验发现 system-DPI-aware + 125% 缩放在部分环境会触发控件不绘制的 bug）。

## 🚀 使用方法

1. 从 [Releases](../../releases) 下载 `EarnPerSecond.exe`（Windows x64）；
2. 双击运行，在设置页填写工资并点击「保存设置」；
3. 点击「开始上班」进入计时页，然后……享受每一秒都在赚钱的感觉；
4. 随时「暂停」或「下班结算」。

> 计算口径：月薪按每月 52/12 ≈ 4.33 个工作周折算；`每秒工资 = 月薪 ÷ (每天小时 × 每周天数 × 4.33 × 3600)`。

## 🛠 从源码构建

### MinGW-w64（推荐）

```bat
build.bat
```

等价命令：

```bat
g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    src\main.cpp -o build\EarnPerSecond.exe -luser32 -lgdi32 -lshell32
```

### MSVC

在 “x64 Native Tools 命令提示符” 中运行：

```bat
build-msvc.bat
```

### GitHub Actions

推送代码后，CI（`.github/workflows/build.yml`）会在 Windows 上自动构建并上传 `EarnPerSecond.exe` 构建产物；打 `v*` 标签会自动生成 Release 并附带可执行文件。

## 📁 项目结构

```
earn-per-second/
├── src/
│   └── main.cpp          # 全部源码（单文件，Win32 GUI，两页式）
├── .github/workflows/
│   └── build.yml         # GitHub Actions 自动构建 + Release
├── build.bat             # MinGW-w64 构建脚本
├── build-msvc.bat        # MSVC 构建脚本
├── LICENSE               # MIT 许可证
└── README.md
```

## 📄 许可证

[MIT](LICENSE) © 2026 ShuiP水瓶

## 🙏 说明

- 界面为中文，源码注释亦为中文；
- 数据仅保存在本地 `salary.ini`，不上传任何信息；
- 本项目仅供娱乐与自我激励，请以合规方式工作与生活 😄
