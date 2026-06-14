# LivePlayer

一个基于 WinAPI + libmpv + yt-dlp 的简易 Windows 视频/直播播放器。

## 功能

- 左侧收藏列表管理常用 URL（添加 / 删除 / 双击播放）
- 使用 libmpv 播放网络视频或直播，依赖 yt-dlp 解析站点
- 解析/播放失败时弹出错误提示
- 关闭窗口即退出程序

## 技术栈

- C11
- CMake（最低 3.20）
- Unicode WinAPI
- libmpv
- yt-dlp

## 目录结构

```
.
├── CMakeLists.txt
├── README.md
└── src
    ├── main.c
    ├── resource.h
    └── resource.rc
```

## 编译

### 准备 libmpv 头文件

1. 下载 Windows 版 libmpv 头文件（`mpv-dev` 中的 `include/mpv/` 目录即可，无需 `lib/mpv.lib`）。
2. 将头文件放在项目根目录的 `mpv/include/mpv/` 文件夹下，或通过 CMake 参数指定：

```powershell
cmake -B build -G "Visual Studio 18 2026" -DMPV_ROOT=C:/path/to/mpv-dev
```

### 使用 Visual Studio 2026 构建

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

或者打开 `build/liveplayer.sln` 在 Visual Studio 中编译。

## 运行

程序启动时会通过 `LoadLibraryW("libmpv-2.dll")` 动态加载 libmpv，因此需要保证 `libmpv-2.dll` 所在目录在系统的 `PATH` 环境变量中，或者将其放在生成的可执行文件同目录（`build/Release/` 或 `build/Debug/`）。

运行前，还需要将以下文件放在可执行文件同目录：

- `yt-dlp.exe`（建议下载独立 exe 版本）

首次运行会自动在可执行文件同目录创建 `favorites.txt` 保存收藏列表。

## 使用

1. 点击“添加”输入视频/直播 URL 并保存到收藏列表。
2. 选中列表项，点击“播放”或双击开始播放。
3. 若站点解析失败，libmpv 会触发错误事件，程序将弹出提示框。

## 配置说明

- 收藏文件：`favorites.txt`，UTF-8 编码，每行一个 URL。
- `yt-dlp.exe` 路径：程序会优先尝试加载同目录下的 `yt-dlp.exe`。
