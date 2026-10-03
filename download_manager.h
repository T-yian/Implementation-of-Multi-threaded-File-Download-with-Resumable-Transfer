#pragma once

#include "common.h"

#include <string>
#include <vector>

// 下载调度器：分片、多线程下载、写入本地文件
class DownloadManager
{
public:
    static bool download(const DownloadConfig &config, std::string &error);

    // 把文件按线程数切成多个 Segment（也供测试/教程引用）
    static std::vector<Segment> splitSegments(int64_t total_size, int thread_count);
};
