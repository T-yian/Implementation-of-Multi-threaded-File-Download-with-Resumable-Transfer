# 设计说明

## 一次完整下载的流程

    probe 探测大小与 Range 支持
      → prepareLocalFile 预分配目标文件（已存在且大小正确则保留）
      → 判断 supports_range
          ├─ 否 → downloadWithoutRange：单线程整文件 GET，无 .meta
          └─ 是 → prepareProgress：加载或新建 .meta
                 → downloadWithProgress：启动 min(线程数, 分片数) 个工作线程
                 → 全部完成后删除 .meta；失败则保留 .meta 供续传

## 断点续传设计

`.meta` 为纯文本，与目标文件同目录同名加 `.meta` 后缀：

    url=https://example.com/file.zip
    total_size=1000000
    thread_count=4
    segment_count=4
    [0]
    start=0
    end=249999
    downloaded=131072
    finished=0

续传生效需同时满足：**URL、总大小、线程数、分片边界全部一致**，任一变化（换文件、改线程数、远程文件大小改变）都会将旧 `.meta` 作废并重新开始，避免把旧进度写到新任务上。

每个分片内部以 256KB 为粒度循环下载，每块完成后：写文件 → 更新进度条 → 加锁更新分片状态并重写 `.meta`。进程被杀时磁盘上已有对应字节，`.meta` 记录了下一次请求的起点。

### 原子替换写入（防断电损坏）

直接覆盖写存在毫秒级窗口：ofstream 打开即清空，写到一半断电会留下半截 `.meta`，而 `.meta` 是唯一账本（数据文件预分配定长，读不出真实进度），损坏即进度清零。

改为：先写 `.meta.tmp` → `flush()` + `close()` 完整落盘 → `rename()`（Linux，POSIX 原子）或 `MoveFileExA(MOVEFILE_REPLACE_EXISTING)`（Windows 历史版本）原子替换。同分区的 rename 只改目录项、不搬数据，文件系统保证全或无 —— 任何时刻 `.meta` 要么旧完整、要么新完整，最坏只残留一个无害的 `.tmp`。

> 原子性消灭的是"损坏"，丢失量仍由 256KB 粒度决定（最多丢每线程最后一块），两者正交。

## 线程模型

| 共享资源 | 同步方式 |
|---------|---------|
| 写入文件的不同偏移 | 无需加锁：分片区间两两不重叠，预分配保证文件尾已到位 |
| `.meta` 文件 | `ProgressTracker::updateSegment` 内部持锁，整份重写 |
| 错误标志与错误信息 | `std::atomic<bool>` + 专属 mutex，只保留第一条错误 |
| 终端输出 | 日志锁、进度条 mutex，防止多线程打印交错不可读 |

线程数大于分片数时多出的线程不启动；线程数小于分片数时剩余分片由主线程在 join 后依次下载。

## 构建

    sudo apt install build-essential cmake libcurl4-openssl-dev
    mkdir -p build && cd build
    cmake .. && make -j$(nproc)

## 本地测试服务器

外网测速站常对单个 IP 限流（连续 Range 请求返回 429），本地回归用自建服务器（实现 206 + Content-Range）：

    python3 range_server.py     # 监听 8001
    ./downloader download "http://127.0.0.1:8001/test100.bin" "local.bin" 4

## 限制与改进方向

- 依赖 `Content-Length`，不支持分块编码（chunked）或动态长度的响应
- 未实现失败重试与指数退避；未处理 HTTP 429 限流的自动退避
- 未支持代理、认证、HTTPS 证书自定义
- 可扩展：指数退避重试、整文件 SHA256 校验、下载速度与 ETA、配置文件替代命令行参数
