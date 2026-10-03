#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct Segment
{
    int64_t start;      // 起始字节（闭区间，含）
    int64_t end;        // 结束字节（闭区间，含）
    int64_t downloaded; // 已下载字节数（相对 start 的偏移）
    bool finished;      // 是否下载完成
};

struct FileInfo
{
    int64_t total_size;   // 文件总大小
    bool supports_range;  // 是否支持 Range 请求
    std::string filename; // 文件名
};

struct DownloadConfig
{
    std::string url;       // 文件下载地址
    std::string save_path; // 文件保存路径
    int thread_count;      // 下载线程数
};