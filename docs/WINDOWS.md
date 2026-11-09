# Windows 版本（历史）

项目最初基于 **WinHTTP** 实现（零第三方依赖，Windows 单平台），2026-11 迁移至 Linux + libcurl，主开发平台变更为 Linux。Windows 版本保留在 git 历史中，可 checkout 对应 commit 查看；仓库内保留的 Windows 构建与演示脚本仅作历史参考，不再维护。

## 构建（Windows）

需要 Windows + MinGW-w64（g++）或任意 C++17 编译器，链接 `winhttp` 与 `ws2_32`：

    .\build.ps1

或直接编译：

    g++ -std=c++17 -O2 main.cpp http_client.cpp file_util.cpp progress.cpp progress_display.cpp download_manager.cpp -lwinhttp -lws2_32 -o build\downloader.exe

## 使用

    .\build\downloader.exe probe "<URL>"
    .\build\downloader.exe download "<URL>" "<保存路径>" 4

`demo.bat` 提供一键演示（probe → 下载），`test_urls/` 内为测试 URL 与脚本。

## 调试注意

`.vscode/` 内含 VS Code 构建任务与 gdb 调试配置（MinGW）。**项目路径不要包含中文**，MinGW gdb 无法处理 UTF-8 路径；`launch.json` 中 `miDebuggerPath` 需与本地 gdb 路径一致。
