# 多线程文件下载及断点续传（Multi-Thread Downloader）

基于 **C++17 + libcurl** 的命令行下载器，主开发平台 Linux。支持多线程分片下载、断点续传、实时进度显示，服务器不支持 Range 时自动降级为单线程。

## 功能特性

- **HEAD 探测**：下载前查询文件大小、是否支持 Range、服务端建议文件名
- **多线程分片下载**：按线程数将文件切为闭区间分片，各线程通过 `Range: bytes=start-end` 并发下载，按绝对偏移写入同一文件
- **断点续传**：以 256KB 为小块下载，每块落盘后即时更新 `.meta` 进度文件；进程中断后重跑相同命令即可续传
- **原子替换写入**：`.meta` 先写临时文件再 `rename` 原子替换，断电不会留下半截账本
- **实时进度条**：`std::atomic` 累加字节数 + `std::mutex` 保护终端输出，`\r` 回车覆盖实现单行刷新
- **安全降级**：服务器不支持 Range 时自动切换单线程整文件 GET；对谎报 Range 的服务器（返回字节数与请求区间不符）直接报错，绝不静默写坏文件

## 快速开始（Linux）

    sudo apt install build-essential cmake libcurl4-openssl-dev
    mkdir -p build && cd build
    cmake .. && make -j$(nproc)

    ./downloader probe "<URL>"
    ./downloader download "<URL>" "<保存路径>" 4     # 线程数可省略，默认 4

中断后原样重跑同一条 download 命令 = 断点续传。

## 架构

    main.cpp                  命令行入口，解析 probe / download 两个子命令
      └── DownloadManager     下载调度：分片、线程管理、分支决策
            ├── HttpClient    libcurl 封装：HEAD 探测、Range / 整文件 GET
            ├── FileUtil      本地文件：预分配、按偏移写入、存在性/大小查询
            ├── ProgressTracker   .meta 进度文件的读写、匹配校验、线程安全更新
            └── ProgressDisplay   终端进度条
    common.h                  Segment / FileInfo / DownloadConfig 三个数据结构

## 文档

- [设计说明](docs/DESIGN.md)：下载流程、断点续传与原子替换、线程模型、本地测试服务器
- [Windows 历史版本](docs/WINDOWS.md)：迁移前的 WinHTTP 实现与构建方式

## 已知限制

依赖 `Content-Length`（不支持 chunked）；未实现失败重试与 429 限流退避；未支持代理与认证。可扩展方向见[设计说明](docs/DESIGN.md#限制与改进方向)。
