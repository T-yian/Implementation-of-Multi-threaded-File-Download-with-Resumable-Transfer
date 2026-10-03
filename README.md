# 多线程文件下载及断点续传（Multi-Thread Downloader）

基于 **C++17 + WinHTTP** 的命令行下载器，零第三方依赖。支持多线程分片下载、断点续传、实时进度显示，服务器不支持 Range 时自动降级为单线程。

## 功能特性

- **HEAD 探测**：下载前查询文件大小、是否支持 Range、服务端建议文件名
- **多线程分片下载**：按线程数将文件切为闭区间分片，各线程通过 `Range: bytes=start-end` 并发下载，按绝对偏移写入同一文件
- **断点续传**：以 256KB 为小块下载，每块落盘后立即更新 `.meta` 进度文件；进程中断后重跑相同命令即可从断点继续
- **实时进度条**：`std::atomic` 累加字节数 + `std::mutex` 保护终端输出，`\r` 回车覆盖实现单行刷新
- **安全降级**：服务器不支持 Range 时自动切换单线程整文件 GET；对谎报 Range 的服务器（返回字节数与请求区间不符）直接报错，绝不静默写坏文件

## 构建与运行

需要 Windows + MinGW-w64（`g++`）或任意 C++17 编译器，链接 `winhttp`。

```powershell
# 一键构建（.ps1 内含完整参数）
.\build.ps1

# 或直接编译
g++ -std=c++17 -O2 main.cpp http_client.cpp file_util.cpp progress.cpp progress_display.cpp download_manager.cpp -lwinhttp -lws2_32 -o build\downloader.exe
```

使用：

```powershell
# 探测远程文件
.\build\downloader.exe probe "<URL>"

# 4 线程下载（线程数可省略，默认 4）
.\build\downloader.exe download "<URL>" "<保存路径>" 4

# 中断后原样重跑 = 断点续传
```

`demo.bat` 提供一键演示（probe → 下载），`test_urls/` 内存放了一组稳定的测试 URL 与脚本，详见 [test_urls/README.md](test_urls/README.md)。

## 架构

```
main.cpp                  命令行入口，解析 probe / download 两个子命令
  └── DownloadManager     下载调度：分片、线程管理、分支决策
        ├── HttpClient    WinHTTP 封装：URL 解析、HEAD 探测、Range/整文件 GET
        ├── FileUtil      本地文件：预分配、按偏移写入、存在性/大小查询
        ├── ProgressTracker  .meta 进度文件的读写、匹配校验、线程安全更新
        └── ProgressDisplay  终端进度条
common.h                  Segment / FileInfo / DownloadConfig 三个数据结构
```

一次完整下载的流程：

```
probe 探测大小与 Range 支持
  → prepareLocalFile 预分配目标文件（已存在且大小正确则保留）
  → 判断 supports_range
      ├─ 否 → downloadWithoutRange：单线程整文件 GET，无 .meta
      └─ 是 → prepareProgress：加载或新建 .meta
             → downloadWithProgress：启动 min(线程数, 分片数) 个工作线程
             → 全部完成后删除 .meta；失败则保留 .meta 供续传
```

## 断点续传设计

`.meta` 为纯文本，与目标文件同目录同名加 `.meta` 后缀：

```
url=https://example.com/file.zip
total_size=1000000
thread_count=4
segment_count=4
[0]
start=0
end=249999
downloaded=131072
finished=0
```

续传生效需同时满足：**URL、总大小、线程数、分片边界全部一致**，任一变化（换文件、改线程数、远程文件大小改变）都会将旧 `.meta` 作废并重新开始，避免把旧进度写到新任务上。

每个分片内部以 256KB 为粒度循环下载，每块完成后：写文件 → 更新进度条 → 加锁更新分片状态并重写 `.meta`。进程被杀时磁盘上已有对应字节，`.meta` 记录了下一次请求的起点。

## 线程模型

| 共享资源 | 同步方式 |
|---------|---------|
| 写入文件的不同偏移 | 无需加锁：分片区间两两不重叠，预分配保证文件尾已到位 |
| `.meta` 文件 | `ProgressTracker::updateSegment` 内部持锁，整份重写 |
| 错误标志与错误信息 | `std::atomic<bool>` + 专属 mutex，只保留第一条错误 |
| 终端输出 | 日志锁、进度条 mutex，防止多线程打印交错不可读 |

线程数大于分片数时多出的线程不启动；线程数小于分片数时剩余分片由主线程在 join 后依次下载。

## 限制与改进方向

- 依赖 `Content-Length`，不支持分块编码（chunked）或动态长度的响应
- 暂无网络超时与失败重试，弱网环境下需手动重跑（重跑即续传）
- 不支持 HTTPS 证书校验的精细化自定义、代理、认证
- 可扩展方向：指数退避重试、整文件 SHA256 校验、下载速度与 ETA 显示、配置文件替代命令行参数

## 调试

`.vscode/` 内含 VS Code 的构建任务与 gdb 调试配置（MinGW）。**注意：项目路径不要包含中文**，MinGW gdb 无法处理 UTF-8 路径；`launch.json` 中 `miDebuggerPath` 需与自己的 gdb 安装路径一致。
