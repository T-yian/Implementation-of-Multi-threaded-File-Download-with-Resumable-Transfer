#pragma once

#include "common.h"

#include <string>
#include <vector>

// WinHTTP 封装：负责 URL 解析、HEAD 探测、Range 下载
class HttpClient
{
public:
    // 解析 URL，发送 HEAD 请求，获取文件大小与是否支持断点续传
    static bool probe(const std::string &url, FileInfo &info, std::string &error);

    // 下载指定字节范围，写入 buffer；返回实际读取字节数，<0 表示失败
    static int64_t downloadRange(const std::string &url,
                                 int64_t range_start,
                                 int64_t range_end,
                                 std::vector<char> &buffer,
                                 std::string &error);

    // 不带 Range 的普通 GET 下载（服务器不支持断点续传时的兜底）
    static int64_t downloadFull(const std::string &url,
                                std::vector<char> &buffer,
                                std::string &error);
};
