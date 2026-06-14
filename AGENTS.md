# LivePlayer - Agent Guide

## 项目概述

LivePlayer 是一个基于 Win32 API + libmpv + yt-dlp 的简易 Windows 视频/直播播放器。

核心特点：

- 主窗口管理收藏列表（名称 + URL）。
- 播放时弹出独立窗口，支持同时打开多个播放窗口。
- 运行时通过 `LoadLibraryW(L"libmpv-2.dll")` 动态加载 libmpv，**不链接 `mpv.lib`**。
- 关闭主窗口会强制关闭所有播放窗口并退出程序。

## 技术栈

- C11
- CMake >= 3.20
- Unicode WinAPI（`UNICODE` / `_UNICODE` 已强制定义）
- Visual Studio 2026（CMake generator: `Visual Studio 18 2026`）
- libmpv（仅使用头文件，运行时依赖 `libmpv-2.dll`）
- yt-dlp（运行时放在可执行文件同目录）

## 目录结构

```
.
├── CMakeLists.txt
├── LICENSE
├── README.md
├── AGENTS.md
├── mpv/
│   └── include/mpv/client.h   # 从 mpv GitHub 获取的 libmpv 头文件
├── src/
│   ├── main.c                 # 主程序：主窗口、播放窗口、收藏夹逻辑
│   ├── mpv_dynamic.c          # libmpv 动态加载封装
│   ├── mpv_dynamic.h
│   ├── resource.h
│   └── resource.rc            # 添加收藏对话框
└── build/
    └── liveplayer.sln         # VS2026 生成的解决方案
```

## 构建方式

必须使用 **PowerShell** + **Visual Studio 2026**，不要使用 MinGW/Bash。

```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

或直接打开 `build/liveplayer.sln` 在 Visual Studio 中编译。

## 编码规范

1. **源文件编码**：所有 `.c` / `.h` / `.rc` 文件必须保存为 **UTF-8 with BOM**。MSVC 在中文 Windows 上默认使用 GBK 代码页，无 BOM 的 UTF-8 会导致中文资源/字符串编译乱码。
2. **API**：统一使用 Unicode 版本的 WinAPI（函数名以 `W` 结尾，字符串字面量使用 `L"..."`）。
3. **字符串输出**：使用 `fwprintf` / `MessageBoxW` / `SetDlgItemTextW` 等宽字符版本。
4. **内存**：收藏列表项通过 `LB_SETITEMDATA` 绑定指针，删除/清空时需要手动 `free`。
5. **新增 libmpv 函数**：
   - 在 `mpv_dynamic.h` 中添加对应的 `typedef` 和 `MpvApi` 成员。
   - 在 `mpv_dynamic.c` 的 `MpvLoad()` 中用 `GetProcAddress` 加载。
   - 调用处使用 `g_mpv.xxx`。

## 运行时依赖

- `libmpv-2.dll`：必须在 `PATH` 中，或放在 `build/Release/`（或 `build/Debug/`）下。
- `yt-dlp.exe`：放在可执行文件同目录。
- `favorites.txt`：首次运行自动创建，格式为 `名称\nURL\n` 循环。

## 常见注意事项

- 不要修改 `CMakeLists.txt` 去静态链接 `mpv.lib`；项目设计为动态加载。
- 播放窗口拥有独立的 `mpv_handle`，事件通过 `WM_MPV_EVENT` 派发到对应窗口。
- 主窗口 `WM_DESTROY` 会先清空收藏夹、再关闭所有播放窗口，然后 `PostQuitMessage`。
